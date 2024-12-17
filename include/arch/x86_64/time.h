#ifndef _SHAMPOOS_ARCH_TIME_
#define _SHAMPOOS_ARCH_TIME_
#ifdef _X86_64_
#include <modules/driver/timer/8254.h>
#include <common/types.h>
enum TIMER_SRC_type {
        TSC_timer = 0x1,
        ACPI_timer = 0x2,
        PIT_timer = 0x4,
        HPET_timer = 0x8,
};
void init_timer(void);
#endif
#endif