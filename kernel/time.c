#include <shampoos/time.h>
#include <shampoos/percpu.h>
#include <modules/log/log.h>
volatile i64 jeffies = 0;
u64 loop_per_jeffies;
DEFINE_PER_CPU(i64, tick_cnt);
/*
    every core have a local apic, so every core have a tick_cnt
    but it must sync with jeffies
*/
__attribute__((optimize("O0"))) void loop_delay(u64 loop_cnt)
{
        while (loop_cnt) {
                loop_cnt--;
        }
}
u64 timer_calibration()
{
        /*
            we use loop to calculate the loop per tick_cnt
            just like that did in linux
        */
        u64 lpj = (1 << 8);
        u64 tick_val;
        u64 round_expo = 0;
        u64 nr_try = 0;
        u64 loopadd;
        tick_val = jeffies;
        while (tick_val == jeffies)
                ; /*wait for next jeffies*/
        tick_val = jeffies;

        do {
                round_expo++;
                for (u64 try_in_round = 0;
                     tick_val == jeffies && try_in_round < (1 << round_expo);
                     try_in_round++) {
                        loop_delay(lpj * round_expo);
                        nr_try += round_expo;
                }
        } while (tick_val == jeffies);

        nr_try -= round_expo;
        loopadd = lpj * round_expo;

        while (loopadd > 1) {
                lpj += loopadd;

                tick_val = jeffies;
                while (tick_val == jeffies)
                        ; /*wait for next jeffies*/
                tick_val = jeffies;

                loop_delay(lpj);

                if (tick_val != jeffies)
                        lpj -= loopadd;
                loopadd >>= 1;
        }
        return lpj;
}
void shampoos_time_init()
{
        percpu(tick_cnt) = jeffies;
        arch_init_timer();
        loop_per_jeffies = timer_calibration();
}
void shampoos_do_time_irq()
{
        percpu(tick_cnt)++;
        /*TODO: maybe need add the lock*/
        if (time_after(percpu(tick_cnt), jeffies)) {
                jeffies = percpu(tick_cnt);
        }
        /*TODO: maybe need add the unlock*/
}
void __udelay(u64 us)
{
        u64 loops = (loop_per_jeffies * us * UDELAY_MUL) >> UDELAY_SHIFT;
        loop_delay(loops);
}
void udelay(u64 us)
{
        while (us > UDELAY_MAX) {
                __udelay(UDELAY_MAX);
                us -= UDELAY_MAX;
        }
        __udelay(us);
}
void mdelay(u64 ms)
{
        udelay(ms * 1000);
}