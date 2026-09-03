#ifndef PMM_H
#define PMM_H

#include "common_headers/types.h"
#include "common_headers/multiboot.h"

#include <stddef.h>

#define PAGE_SIZE 4096

void pmm_init(multiboot_info_t *mbi, uintptr_t multiboot_phys);
uintptr_t pmm_alloc_frame(void);
uintptr_t pmm_alloc_frames(size_t count);
void pmm_free_frame(uintptr_t addr);
void pmm_free_range(uintptr_t addr, size_t count);

size_t pmm_total_frames(void);
size_t pmm_usable_frames(void);
size_t pmm_used_frames(void);
size_t pmm_free_frames(void);

uintptr_t pmm_get_bitmap_start(void);
uintptr_t pmm_get_bitmap_end(void);

#endif
