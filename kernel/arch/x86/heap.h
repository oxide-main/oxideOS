#ifndef HEAP_H
#define HEAP_H

#include "common_headers/types.h"
#include <stddef.h>

void heap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);
void *kcalloc(uint32_t num, uint32_t size);
void *krealloc(void *ptr, uint32_t new_size);

uint32_t heap_total_bytes(void);
uint32_t heap_used_bytes(void);
uint32_t heap_free_bytes(void);

void heap_map_all_blocks(void);

#endif
