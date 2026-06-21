#include <rendezvos/time.h>
#include <rendezvos/smp/percpu.h>
volatile i64 jeffies = 0;
u64 loop_per_jeffies;
u64 udelay_max_loop;
u64 heartbeat_gap;
u64 clock_hz;
enum timer_type sys_timer_type = TIMER_TYPE_ONE_SHOT;
DEFINE_PER_CPU(u64, tick_cnt);
DEFINE_PER_CPU(u64, boot_base_time);
/*
    every core have a local apic, so every core have a tick_cnt
    but it must sync with jeffies
*/
__attribute__((optimize("O0"))) u64 loop_delay(volatile u64 loop_cnt)
{
        u64 cnt = loop_cnt;
        while (loop_cnt--)
                ;
        return cnt;
}
static inline u64 timer_calibration(void)
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
void rendezvos_time_init(void)
{
        percpu(tick_cnt) = jeffies;
        register_irq_handler(
                timer_irq_num, rendezvos_do_time_irq, IRQ_NEED_EOI);
        bool is_bsp = (percpu(cpu_number) == BSP_ID);
        heartbeat_gap = arch_init_timer(is_bsp);
        if (is_bsp){
                loop_per_jeffies = timer_calibration();
                clock_hz = arch_timer_get_hz();
        }
        percpu(boot_base_time) = arch_timer_read();
        udelay_max_loop = (loop_per_jeffies * UDELAY_MAX * UDELAY_MUL)
                          >> UDELAY_SHIFT;
}
void rendezvos_do_time_irq(struct trap_frame *tf)
{
        (void)tf;
        percpu(tick_cnt)++;
        /*TODO: maybe need add the lock*/
        if (time_after(percpu(tick_cnt), jeffies)) {
                jeffies = percpu(tick_cnt);
        }
        /*TODO: maybe need add the unlock*/
        arch_reset_timer(heartbeat_gap);
}
tick_t rendezvos_time_now(void)
{
        return arch_timer_read() - percpu(boot_base_time);
}
u64 rendezvos_time_count_to_us(tick_t count)
{
        if (!clock_hz)
                return 0;
        return (count / clock_hz) * 1000000ULL
               + (count % clock_hz) * 1000000ULL / clock_hz;
}
u64 rendezvos_time_count_to_ms(tick_t count)
{
        if (!clock_hz)
                return 0;
        return (count / clock_hz) * 1000ULL
               + (count % clock_hz) * 1000ULL / clock_hz;
}
tick_t rendezvos_time_us_to_count(u64 us)
{
        if (!clock_hz)
                return 0;
        return us * clock_hz / 1000000ULL;
}
tick_t rendezvos_time_ms_to_count(u64 ms)
{
        if (!clock_hz)
                return 0;
        return ms * clock_hz / 1000ULL;
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