#ifndef GDT_H
#define GDT_H

#include "common_headers/types.h"

struct gdt_descriptor_32 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access_byte;
    uint8_t flags_limit;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GDT_KCODE_SEL  0x08
#define GDT_KDATA_SEL  0x10

void encode_gdt_entry_32(struct gdt_descriptor_32* entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);

void gdt_init(void);

extern void gdt_flush(uint32_t gdt_ptr_addr);

#endif
