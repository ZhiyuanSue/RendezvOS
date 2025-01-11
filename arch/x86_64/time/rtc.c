/*
    This file is used to read the RTC
*/
#include <common/bit.h>
#include <arch/x86_64/io_port.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>
struct rtc_time get_rtc_time()
{
        struct rtc_time res;
        u8 rtc_data;
        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_SEC_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.sec = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_MIN_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.min = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_HOUR_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.hour = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_DAY_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.day = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_MONTH_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.month = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_YEAR_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.year = BCD_TO_BIN(rtc_data);

        outb(_X86_RTC_CMOS_RAM_IDX_REG_, _X86_RTC_CURR_CENTURY_REG);
        rtc_data = inb(_X86_RTC_CMOS_RAM_DATA_);
        res.year += 100 * BCD_TO_BIN(rtc_data);

        pr_info("[ RTC ] %02d-%02d-%02d %02d:%02d:%02d\n",
                res.year,
                res.month,
                res.day,
                res.hour,
                res.min,
                res.sec);
        return res;
}