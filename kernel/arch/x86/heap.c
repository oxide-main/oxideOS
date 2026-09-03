#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "common_headers/string.h"

#define HEAP_MAGIC      0x4B484550
#define BLOCK_ALIGN     8
#define HEADER_SIZE     ((uint32_t) sizeof(heap_block_t))
#define MIN_SPLIT_SIZE  (HEADER_SIZE + BLOCK_ALIGN)

typedef struct heap_block {
    uint32_t            magic;
    uint32_t            size;
    int32_t             free;
    struct heap_block  *prev;
    struct heap_block  *next;
    uint32_t            _pad;
} heap_block_t;

static heap_block_t *head;
static uint32_t      heap_total;
static uint32_t      heap_alloc;

static inline uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) & ~(a - 1);
}

static heap_block_t *heap_grow(uint32_t min_payload)
{
    if (min_payload > 0xFFFFFFFF - PAGE_SIZE - HEADER_SIZE) {
        return 0;
    }
    uint32_t need = align_up(HEADER_SIZE + min_payload, PAGE_SIZE);
    uint32_t pages = need / PAGE_SIZE;
    if (pages == 0) {
        pages = 1;
    }

    uintptr_t base = pmm_alloc_frames(pages);
    if (base == 0) {
        return 0;
    }

    if (paging_is_enabled()) {
        for (uint32_t i = 0; i < pages; i++) {
            uintptr_t paddr = base + (uintptr_t) i * PAGE_SIZE;
            paging_map_page(paddr, paddr, PAGE_PRESENT | PAGE_WRITABLE);
        }
    }

    uint32_t total = pages * PAGE_SIZE;

    heap_block_t *tail = head;
    if (tail) {
        while (tail->next) {
            tail = tail->next;
        }
    }

    if (tail && tail->free) {
        uintptr_t tail_end = (uintptr_t) tail + HEADER_SIZE + tail->size;
        if (tail_end == base) {
            tail->size += total;
            heap_total += total;
            return tail;
        }
    }

    heap_block_t *blk = (heap_block_t *) base;
    blk->magic = HEAP_MAGIC;
    blk->size  = total - HEADER_SIZE;
    blk->free  = 1;
    blk->prev  = tail;
    blk->next  = 0;
    blk->_pad  = 0;

    if (tail) {
        tail->next = blk;
    } else {
        head = blk;
    }

    heap_total += blk->size;
    return blk;
}

static void split_block(heap_block_t *blk, uint32_t size)
{
    uint32_t remaining = blk->size - size - HEADER_SIZE;
    heap_block_t *new_blk = (heap_block_t *) ((uint8_t *) blk + HEADER_SIZE + size);

    new_blk->magic = HEAP_MAGIC;
    new_blk->size  = remaining;
    new_blk->free  = 1;
    new_blk->prev  = blk;
    new_blk->next  = blk->next;
    new_blk->_pad  = 0;

    if (blk->next) {
        blk->next->prev = new_blk;
    }
    blk->next = new_blk;
    blk->size = size;

    heap_total -= HEADER_SIZE;
}

static heap_block_t *coalesce(heap_block_t *blk)
{
    if (blk->next && blk->next->magic == HEAP_MAGIC && blk->next->free) {
        uint8_t *expected_next = (uint8_t *) blk + HEADER_SIZE + blk->size;
        if ((uint8_t *) blk->next == expected_next) {
            heap_block_t *n = blk->next;
            blk->size += HEADER_SIZE + n->size;
            blk->next = n->next;
            if (n->next) {
                n->next->prev = blk;
            }
            n->magic = 0;
            heap_total += HEADER_SIZE;
        }
    }

    if (blk->prev && blk->prev->magic == HEAP_MAGIC && blk->prev->free) {
        uint8_t *expected_blk = (uint8_t *) blk->prev + HEADER_SIZE + blk->prev->size;
        if ((uint8_t *) blk == expected_blk) {
            heap_block_t *p = blk->prev;
            p->size += HEADER_SIZE + blk->size;
            p->next = blk->next;
            if (blk->next) {
                blk->next->prev = p;
            }
            blk->magic = 0;
            heap_total += HEADER_SIZE;
            blk = p;
        }
    }

    return blk;
}

void heap_init(void)
{
    head       = 0;
    heap_total = 0;
    heap_alloc = 0;

    heap_grow(PAGE_SIZE - HEADER_SIZE);
}

void *kmalloc(uint32_t size)
{
    if (size == 0 || size > 0xE0000000) {
        return 0;
    }

    size = align_up(size, BLOCK_ALIGN);

    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags));

    heap_block_t *blk = head;
    while (blk) {
        if (blk->magic == HEAP_MAGIC && blk->free && blk->size >= size) {
            break;
        }
        blk = blk->next;
    }

    if (!blk) {
        blk = heap_grow(size);
        if (!blk) {
            __asm__ volatile ("pushl %0; popfl" : : "r"(flags));
            return 0;
        }
    }

    if (blk->size >= size + MIN_SPLIT_SIZE) {
        split_block(blk, size);
    }

    blk->free = 0;
    heap_alloc += blk->size;

    __asm__ volatile ("pushl %0; popfl" : : "r"(flags));

    return (void *) ((uint8_t *) blk + HEADER_SIZE);
}

void kfree(void *ptr)
{
    if (!ptr) {
        return;
    }

    if (((uintptr_t) ptr % BLOCK_ALIGN) != 0) {
        return;
    }

    heap_block_t *blk = (heap_block_t *) ((uint8_t *) ptr - HEADER_SIZE);
    if (blk->magic != HEAP_MAGIC || blk->free) {
        return;
    }

    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags));

    blk->free = 1;
    if (heap_alloc >= blk->size) {
        heap_alloc -= blk->size;
    } else {
        heap_alloc = 0;
    }

    coalesce(blk);

    __asm__ volatile ("pushl %0; popfl" : : "r"(flags));
}

void *kcalloc(uint32_t num, uint32_t size)
{
    uint64_t total = (uint64_t) num * size;
    if (total > 0xFFFFFFFF) {
        return 0;
    }

    void *ptr = kmalloc((uint32_t) total);
    if (ptr) {
        memset(ptr, 0, (uint32_t) total);
    }
    return ptr;
}

void *krealloc(void *ptr, uint32_t new_size)
{
    if (!ptr) {
        return kmalloc(new_size);
    }
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }

    if (((uintptr_t) ptr % BLOCK_ALIGN) != 0) {
        return 0;
    }

    heap_block_t *blk = (heap_block_t *) ((uint8_t *) ptr - HEADER_SIZE);
    if (blk->magic != HEAP_MAGIC || blk->free) {
        return 0;
    }

    uint32_t aligned_new = align_up(new_size, BLOCK_ALIGN);
    if (blk->size >= aligned_new) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return 0;
    }

    memcpy(new_ptr, ptr, blk->size);
    kfree(ptr);
    return new_ptr;
}

uint32_t heap_total_bytes(void)
{
    return heap_total;
}

uint32_t heap_used_bytes(void)
{
    return heap_alloc;
}

uint32_t heap_free_bytes(void)
{
    return heap_total > heap_alloc ? heap_total - heap_alloc : 0;
}

void heap_map_all_blocks(void)
{
    heap_block_t *blk = head;
    while (blk) {
        uintptr_t start = (uintptr_t) blk;
        uintptr_t total = HEADER_SIZE + blk->size;
        uintptr_t end = start + total;
        for (uintptr_t p = (start & PAGE_FRAME_MASK); p < end; p += PAGE_SIZE) {
            paging_map_page(p, p, PAGE_PRESENT | PAGE_WRITABLE);
        }
        blk = blk->next;
    }
}
