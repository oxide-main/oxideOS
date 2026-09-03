#include "gdt.h"

void encode_gdt_entry_32(struct gdt_descriptor_32* entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) 
{
    entry->limit_low = (limit & 0xFFFF);
    entry->base_low = (base & 0xFFFF);
    entry->base_middle = (base >> 16) & 0xFF;
    entry->access_byte = access;
    entry->flags_limit = ((flags & 0x0F) << 4) | ((limit >> 16) & 0x0F);
    entry->base_high = (base >> 24) & 0xFF;
}

static struct gdt_descriptor_32 gdt_entries[5];
static struct gdt_ptr gp;

void gdt_init(void)
{
    gp.limit = (sizeof(struct gdt_descriptor_32) * 5) - 1;
    gp.base = (uint32_t) &gdt_entries;

    encode_gdt_entry_32(&gdt_entries[0], 0, 0, 0, 0);

    encode_gdt_entry_32(&gdt_entries[1], 0, 0xFFFFF, 0x9A, 0x0C);

    encode_gdt_entry_32(&gdt_entries[2], 0, 0xFFFFF, 0x92, 0x0C);

    encode_gdt_entry_32(&gdt_entries[3], 0, 0xFFFFF, 0xFA, 0x0C);

    encode_gdt_entry_32(&gdt_entries[4], 0, 0xFFFFF, 0xF2, 0x0C);

    gdt_flush((uint32_t) &gp);
}
