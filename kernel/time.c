#include <shampoos/time.h>
#include <shampoos/percpu.h>
#include <modules/log/log.h>
volatile i64 jeffies = 0;

DEFINE_PER_CPU(i64, tick_cnt);
/*
    every core have a local apic, so every core have a tick_cnt
    but it must sync with jeffies
*/
void shampoos_time_init()
{
        pr_info("addr is %x %x\n", &percpu(tick_cnt), &tick_cnt);
        percpu(tick_cnt) = jeffies;
        arch_init_timer();
}
void shampoos_do_time_irq()
{
        percpu(tick_cnt)++;
        /*TODO: maybe need add the lock*/
        if (time_after(percpu(tick_cnt),jeffies)) {
                jeffies = percpu(tick_cnt);
        }
        /*TODO: maybe need add the unlock*/
}
u64 timer_calibration()
{
        /*
            we use loop to calculate the loop per tick_cnt
            just like that did in linux
        */
        u64 lpj = 0;

        return lpj;
}