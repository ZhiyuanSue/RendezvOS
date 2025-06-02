#include <rendezvos/time.h>
#include <rendezvos/smp/percpu.h>
volatile i64 jeffies = 0;
u64 loop_per_jeffies;
u64 udelay_max_loop;
DEFINE_PER_CPU(i64, tick_cnt);
/*
    every core have a local apic, so every core have a tick_cnt
    but it must sync with jeffies
*/
__attribute__((optimize("O0"))) u64 loop_delay(volatile u64 loop_cnt)
{
        volatile u64 cnt = 0;
        while (loop_cnt--) {
                cnt++;
        }
        return cnt;
}
__attribute__((optimize("O0"))) u64 timer_calibration()
{
        volatile u64 lpj = 0;
        i64 tick_val;
#define LPJ_CALIBRATION_CNT 25
        for (int i = 0; i < LPJ_CALIBRATION_CNT; i++) {
                tick_val = jeffies;
                while (tick_val == jeffies)
                        ; /*wait for next jeffies*/
                tick_val = jeffies;

                while (tick_val == jeffies) {
                        lpj++;
                }
        }
        return lpj / 25;
}
void rendezvos_time_init()
{
        percpu(tick_cnt) = jeffies;
        register_irq_handler(
                timer_irq_num, rendezvos_do_time_irq, IRQ_NEED_EOI);
        arch_init_timer();
        loop_per_jeffies = timer_calibration();
        udelay_max_loop = (loop_per_jeffies * UDELAY_MAX * UDELAY_MUL)
                          >> UDELAY_SHIFT;
}
void rendezvos_do_time_irq(struct trap_frame *tf)
{
        percpu(tick_cnt)++;
        /*TODO: maybe need add the lock*/
        if (time_after(percpu(tick_cnt), jeffies)) {
                jeffies = percpu(tick_cnt);
        }
        /*TODO: maybe need add the unlock*/
        arch_reset_timer();
}
void __udelay(u64 lpj, u64 us)
{
        u64 loops = (lpj * us * UDELAY_MUL) >> UDELAY_SHIFT;
        loop_delay(loops);
}
void _udelay(u64 uml, u64 lpj, volatile u64 us)
{
        while (us >= UDELAY_MAX) {
                loop_delay(uml);
                us -= UDELAY_MAX;
        }
        __udelay(lpj, us);
}
void udelay(u64 us)
{
        _udelay(udelay_max_loop, loop_per_jeffies, us);
}
void mdelay(u64 ms)
{
        _udelay(udelay_max_loop, loop_per_jeffies, ms * 1000);
}