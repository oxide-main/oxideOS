#ifndef PAGING_H
#define PAGING_H

#include "common_headers/types.h"
#include "common_headers/multiboot.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE           4096

#define PAGE_PRESENT        (1u << 0)
#define PAGE_WRITABLE       (1u << 1)
#define PAGE_USER           (1u << 2)
#define PAGE_WRITE_THROUGH  (1u << 3)
#define PAGE_CACHE_DISABLE  (1u << 4)
#define PAGE_ACCESSED       (1u << 5)
#define PAGE_DIRTY          (1u << 6)
#define PAGE_4MB            (1u << 7)
#define PAGE_GLOBAL         (1u << 8)

#define PAGE_FRAME_MASK     0xFFFFF000u
#define PAGE_FLAGS_MASK     0x00000FFFu

#define PAGING_SCRATCH_PAGE 0x003FF000u

void paging_init(multiboot_info_t *mbi, uintptr_t multiboot_phys);
int  paging_map_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint32_t flags);
void paging_unmap_page(uintptr_t virtual_addr);
uintptr_t paging_get_physical(uintptr_t virtual_addr);
int  paging_is_enabled(void);
void paging_enable(void);
void paging_invlpg(uintptr_t virtual_addr);
uintptr_t paging_get_directory_phys(void);
int  paging_run_tests(void);

#endif
