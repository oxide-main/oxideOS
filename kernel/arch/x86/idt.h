#ifndef IDT_H
#define IDT_H

#include "common_headers/types.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_FLAG_INT_GATE_R0 0x8E

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

void idt_init(void);

extern void idt_flush(uint32_t idt_ptr_addr);

#endif
