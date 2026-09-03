#include "isr.h"
#include "drivers/pic.h"
#include "drivers/vga.h"

static isr_t interrupt_handlers[256];

static const char* exception_messages[32] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

void register_interrupt_handler(uint8_t n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

void isr_handler(registers_t* regs)
{
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
        return;
    }

    uint8_t color = vga_entry_color(RED, BLACK);
    int line = vga_put_chars("\n[PANIC] Unhandled exception: ", color, VGA_HEIGHT - 2);
    if (regs->int_no < 32) {
        vga_put_chars((char*) exception_messages[regs->int_no], color, line - 1);
    }

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void irq_handler(registers_t* regs)
{
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
    }

    pic_send_eoi(regs->int_no - 32);
}
