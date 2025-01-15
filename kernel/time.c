#include <shampoos/time.h>
#include <shampoos/percpu.h>
#include <modules/log/log.h>
volatile i64 jeffies = 0;
u64 loop_per_jeffies;
u64 udelay_max_loop;
DEFINE_PER_CPU(i64, tick_cnt);
/*
    every core have a local apic, so every core have a tick_cnt
    but it must sync with jeffies
*/
extern void arch_loop_delay(u64 us);
extern void _udelay(u64 uml, u64 lpj, u64 us);
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
                        arch_loop_delay(lpj * round_expo);
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

                arch_loop_delay(lpj);

                if (tick_val != jeffies)
                        lpj -= loopadd;
                loopadd >>= 1;
        }
        pr_info("lpj is %d\n", lpj);
        return lpj;
}
void shampoos_time_init()
{
        percpu(tick_cnt) = jeffies;
        arch_init_timer();
        loop_per_jeffies = timer_calibration();
        udelay_max_loop = (loop_per_jeffies * UDELAY_MAX * UDELAY_MUL)
                          >> UDELAY_SHIFT;
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
// void __udelay(u64 lpj,u64 us)
// {
//         u64 loops = (lpj * us * UDELAY_MUL) >> UDELAY_SHIFT;
//         arch_loop_delay(loops);
// }
// void _udelay(u64 uml,u64 lpj,u64 us)
// {
//         while (us > UDELAY_MAX) {
//                 arch_loop_delay(udelay_max_loop);
//                 us -= UDELAY_MAX;
//         }
//         __udelay(lpj,us);
// }
void udelay(u64 us)
{
        _udelay(udelay_max_loop, loop_per_jeffies, us);
}
void mdelay(u64 ms)
{
        _udelay(udelay_max_loop, loop_per_jeffies, ms * 1000);
}