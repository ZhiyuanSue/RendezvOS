#ifndef _SHAMPOOS_ARCH_TIME_
#define _SHAMPOOS_ARCH_TIME_
#include <modules/driver/timer/8254.h>
#include <common/types.h>
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
void init_timer(void);
struct rtc_time get_rtc_time();
#endif