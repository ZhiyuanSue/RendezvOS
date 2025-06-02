#include <arch/aarch64/time.h>
#include <arch/aarch64/sync/barrier.h>
#include <arch/aarch64/trap/trap.h>
#include <modules/log/log.h>
#include <rendezvos/time.h>
// TODO:fix this include problem in vscode
u32 timer_irq_num = AARCH64_IRQ_TO_TRAP_ID(30);
u64 time_irq_cycle;
tick_t get_phy_cnt()
{
        tick_t cntpct_el0_val;
        isb();
        mrs("CNTPCT_EL0", cntpct_el0_val);
        return cntpct_el0_val;
}
tick_t get_virt_cnt()
{
        tick_t cntvct_el0_val;
        isb();
        mrs("CNTVCT_EL0", cntvct_el0_val);
        return cntvct_el0_val;
}
tick_t get_cval()
{
        tick_t cntv_cval;
        isb();
        mrs("CNTV_CVAL_EL0", cntv_cval);
        return cntv_cval;
}
void arch_init_timer(void)
{
        /*here we use the compare value way*/
        u64 timer_freq;
        u64 time_ctrl_value = CNTV_CTL_EL0_ENABLE;
        mrs("CNTFRQ_EL0", timer_freq);

        time_irq_cycle = timer_freq / INT_PER_SECOND;
        msr("CNTP_TVAL_EL0", time_irq_cycle);
        msr("CNTP_CTL_EL0", time_ctrl_value);
}
void arch_reset_timer(void)
{
        msr("CNTP_TVAL_EL0", time_irq_cycle);
}