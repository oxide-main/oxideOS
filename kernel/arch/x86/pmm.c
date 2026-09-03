#include "pmm.h"
#include "common_headers/string.h"

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;

static uint8_t *alloc_bitmap;
static uint8_t *reserved_bitmap;
static uint32_t bitmap_size;
static uint32_t total_frames_count;
static uint32_t usable_frames_count;
static uint32_t used_frames_count;

static inline void bitmap_set(uint8_t *bm, uint32_t frame)
{
    bm[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static inline void bitmap_clear(uint8_t *bm, uint32_t frame)
{
    bm[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static inline int bitmap_test(const uint8_t *bm, uint32_t frame)
{
    return (bm[frame / 8] >> (frame % 8)) & 1;
}

static void mark_region_reserved(uintptr_t base, uintptr_t length)
{
    if (length == 0) {
        return;
    }

    uint32_t start_frame = (uint32_t)(base / PAGE_SIZE);
    uint64_t end_addr = (uint64_t) base + length;
    uint32_t end_frame = (uint32_t)((end_addr + PAGE_SIZE - 1) / PAGE_SIZE);

    if (start_frame >= total_frames_count) {
        return;
    }
    if (end_frame > total_frames_count) {
        end_frame = total_frames_count;
    }

    for (uint32_t f = start_frame; f < end_frame; f++) {
        if (!bitmap_test(alloc_bitmap, f)) {
            bitmap_set(alloc_bitmap, f);
            used_frames_count++;
        }
        bitmap_set(reserved_bitmap, f);
    }
}

void pmm_init(multiboot_info_t *mbi, uintptr_t multiboot_phys)
{
    uint64_t highest_usable_addr = 0;

    if (!mbi || !(mbi->flags & MULTIBOOT_FLAG_MMAP) || mbi->mmap_length == 0 || mbi->mmap_addr == 0) {
        total_frames_count = 0;
        usable_frames_count = 0;
        used_frames_count = 0;
        alloc_bitmap = 0;
        reserved_bitmap = 0;
        return;
    }

    uintptr_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
    for (uintptr_t off = mbi->mmap_addr; off < mmap_end;) {
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *) off;
        if (entry->type == MULTIBOOT_MMAP_AVAILABLE && entry->addr < 0x100000000ULL) {
            uint64_t region_end = entry->addr + entry->len;
            if (region_end > 0x100000000ULL) {
                region_end = 0x100000000ULL;
            }
            if (region_end > highest_usable_addr) {
                highest_usable_addr = region_end;
            }
        }
        off += entry->size + 4;
    }

    if (highest_usable_addr == 0) {
        total_frames_count = 0;
        usable_frames_count = 0;
        used_frames_count = 0;
        alloc_bitmap = 0;
        reserved_bitmap = 0;
        return;
    }

    total_frames_count = (uint32_t)(highest_usable_addr / PAGE_SIZE);
    bitmap_size = (total_frames_count + 7) / 8;
    bitmap_size = (bitmap_size + 3) & ~3u;

    alloc_bitmap = &_kernel_end;
    reserved_bitmap = alloc_bitmap + bitmap_size;

    memset(alloc_bitmap, 0xFF, bitmap_size);
    memset(reserved_bitmap, 0xFF, bitmap_size);
    usable_frames_count = 0;
    used_frames_count = 0;

    for (uintptr_t off = mbi->mmap_addr; off < mmap_end;) {
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *) off;
        if (entry->type == MULTIBOOT_MMAP_AVAILABLE && entry->addr < 0x100000000ULL) {
            uint64_t base = entry->addr;
            uint64_t end = base + entry->len;
            if (end > 0x100000000ULL) {
                end = 0x100000000ULL;
            }
            uint32_t start_frame = (uint32_t)((base + PAGE_SIZE - 1) / PAGE_SIZE);
            uint32_t end_frame = (uint32_t)(end / PAGE_SIZE);
            if (end_frame > total_frames_count) {
                end_frame = total_frames_count;
            }
            for (uint32_t f = start_frame; f < end_frame; f++) {
                if (bitmap_test(alloc_bitmap, f)) {
                    bitmap_clear(alloc_bitmap, f);
                    bitmap_clear(reserved_bitmap, f);
                    usable_frames_count++;
                }
            }
        }
        off += entry->size + 4;
    }

    for (uintptr_t off = mbi->mmap_addr; off < mmap_end;) {
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *) off;
        if (entry->type != MULTIBOOT_MMAP_AVAILABLE && entry->addr < 0x100000000ULL) {
            uint64_t len = entry->len;
            if (entry->addr + len > 0x100000000ULL) {
                len = 0x100000000ULL - entry->addr;
            }
            mark_region_reserved((uintptr_t) entry->addr, (uintptr_t) len);
        }
        off += entry->size + 4;
    }

    mark_region_reserved(0, PAGE_SIZE);

    uintptr_t kernel_start = (uintptr_t) &_kernel_start;
    uintptr_t kernel_end = (uintptr_t) &_kernel_end;
    mark_region_reserved(kernel_start, kernel_end - kernel_start);

    mark_region_reserved((uintptr_t) alloc_bitmap, bitmap_size * 2);

    mark_region_reserved(multiboot_phys, sizeof(multiboot_info_t));

    if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
        mark_region_reserved(mbi->mmap_addr, mbi->mmap_length);
    }

    if ((mbi->flags & MULTIBOOT_FLAG_CMDLINE) && mbi->cmdline != 0) {
        mark_region_reserved(mbi->cmdline, strlen((const char *) mbi->cmdline) + 1);
    }

    if ((mbi->flags & MULTIBOOT_FLAG_MODS) && mbi->mods_count > 0 && mbi->mods_addr != 0) {
        mark_region_reserved(mbi->mods_addr, mbi->mods_count * sizeof(multiboot_module_t));
        multiboot_module_t *mods = (multiboot_module_t *) mbi->mods_addr;
        for (uint32_t m = 0; m < mbi->mods_count; m++) {
            if (mods[m].mod_end > mods[m].mod_start) {
                mark_region_reserved(mods[m].mod_start, mods[m].mod_end - mods[m].mod_start);
            }
            if (mods[m].cmdline != 0) {
                mark_region_reserved(mods[m].cmdline, strlen((const char *) mods[m].cmdline) + 1);
            }
        }
    }

    if ((mbi->flags & MULTIBOOT_FLAG_ELF) && mbi->syms[2] != 0 && mbi->syms[0] > 0 && mbi->syms[1] > 0) {
        mark_region_reserved(mbi->syms[2], mbi->syms[0] * mbi->syms[1]);
    }

    if ((mbi->flags & MULTIBOOT_FLAG_LOADER) && mbi->boot_loader_name != 0) {
        mark_region_reserved(mbi->boot_loader_name, strlen((const char *) mbi->boot_loader_name) + 1);
    }
}

uintptr_t pmm_alloc_frames(size_t count)
{
    if (!alloc_bitmap || count == 0 || count > (usable_frames_count - used_frames_count)) {
        return 0;
    }

    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags));

    uint32_t consecutive = 0;
    uint32_t start_frame = 0;

    for (uint32_t f = 1; f < total_frames_count; f++) {
        if (!bitmap_test(alloc_bitmap, f)) {
            if (consecutive == 0) {
                start_frame = f;
            }
            consecutive++;
            if (consecutive == count) {
                for (uint32_t i = 0; i < count; i++) {
                    bitmap_set(alloc_bitmap, start_frame + i);
                }
                used_frames_count += count;
                __asm__ volatile ("pushl %0; popfl" : : "r"(flags));
                return (uintptr_t) start_frame * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }

    __asm__ volatile ("pushl %0; popfl" : : "r"(flags));
    return 0;
}

uintptr_t pmm_alloc_frame(void)
{
    return pmm_alloc_frames(1);
}

void pmm_free_range(uintptr_t addr, size_t count)
{
    if (!alloc_bitmap || addr == 0 || (addr % PAGE_SIZE) != 0 || count == 0) {
        return;
    }

    uint32_t start_frame = (uint32_t)(addr / PAGE_SIZE);
    if (start_frame == 0 || start_frame >= total_frames_count) {
        return;
    }
    if ((uint64_t) start_frame + count > total_frames_count) {
        return;
    }

    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags));

    for (size_t i = 0; i < count; i++) {
        uint32_t f = start_frame + i;
        if (bitmap_test(reserved_bitmap, f)) {
            continue;
        }
        if (!bitmap_test(alloc_bitmap, f)) {
            continue;
        }
        bitmap_clear(alloc_bitmap, f);
        if (used_frames_count > 0) {
            used_frames_count--;
        }
    }

    __asm__ volatile ("pushl %0; popfl" : : "r"(flags));
}

void pmm_free_frame(uintptr_t addr)
{
    pmm_free_range(addr, 1);
}

size_t pmm_total_frames(void)
{
    return total_frames_count;
}

size_t pmm_usable_frames(void)
{
    return usable_frames_count;
}

size_t pmm_used_frames(void)
{
    return used_frames_count;
}

size_t pmm_free_frames(void)
{
    return usable_frames_count > used_frames_count ? usable_frames_count - used_frames_count : 0;
}

uintptr_t pmm_get_bitmap_start(void)
{
    return (uintptr_t) alloc_bitmap;
}

uintptr_t pmm_get_bitmap_end(void)
{
    return (uintptr_t) reserved_bitmap + bitmap_size;
}
