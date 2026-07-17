#ifndef _RENDEZVOS_ARCH_TIME_
#define _RENDEZVOS_ARCH_TIME_
#include <arch/x86_64/PIC/PIT.h>
#include <common/types.h>
#include <common/stdbool.h>
#include <arch/x86_64/sys_ctrl.h>
enum TIMER_SRC_type {
        TSC_timer = 0x1,
        ACPI_timer = 0x2,
        PIT_timer = 0x4,
        HPET_timer = 0x8,
};
struct rtc_time {
        u64 rtc_value;
        struct {
                u8 sec;
                u8 min;
                u8 hour;
                u8 day;
                u16 month; /*here use u16,just for align*/
                u16 year;
        };
};
u64 arch_init_timer(bool is_bsp);
struct rtc_time get_rtc_time();
#endif