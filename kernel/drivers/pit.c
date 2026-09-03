#include "pit.h"
#include "pic.h"
#include "common_headers/io.h"
#include "arch/x86/isr.h"

static volatile uint32_t tick_lo;
static volatile uint32_t tick_hi;

static void pit_irq_handler(registers_t* regs)
{
    (void) regs;
    tick_lo++;
    if (tick_lo == 0) {
        tick_hi++;
    }
}

void pit_init(void)
{
    uint16_t divisor = PIT_BASE_FREQ / PIT_TARGET_HZ;

    /* channel 0, lobyte/hibyte, rate generator (mode 2) */
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor >> 8));

    register_interrupt_handler(IRQ0_VECTOR, pit_irq_handler);
    pic_clear_mask(0);
}

uint64_t pit_get_ticks(void)
{
    uint32_t hi, lo;
    uint32_t flags;

    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags));
    hi = tick_hi;
    lo = tick_lo;
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags));

    return ((uint64_t) hi << 32) | lo;
}

uint32_t pit_get_uptime_ms(void)
{
    uint64_t ticks = pit_get_ticks();
    return (uint32_t)(ticks * 1000 / PIT_TARGET_HZ);
}
