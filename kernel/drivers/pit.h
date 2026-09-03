#ifndef PIT_H
#define PIT_H

#include "common_headers/types.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQ     1193182
#define PIT_TARGET_HZ     100

void pit_init(void);
uint64_t pit_get_ticks(void);
uint32_t pit_get_uptime_ms(void);

#endif
