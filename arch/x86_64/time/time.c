#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/time.h>

extern enum IRQ_type arch_irq_type;
extern enum timer_type sys_timer_type;
u32 timer_irq_num = _8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_;
void arch_init_timer(bool is_bsp)
{
        if (arch_irq_type == NO_IRQ) {
                return;
        } else if (arch_irq_type == PIC_IRQ) {
                if (is_bsp) {
                        init_8254_cyclical(1000);
                        enable_PIC_IRQ(timer_irq_num);
                }
        } else if (arch_irq_type == xAPIC_IRQ) {
                if (is_bsp) {
                        if (TSC_DDL_support()) {
                                pr_info("tsc ddl mode is supported\n");
                                sys_timer_type = TIMER_TYPE_X86_TSC_DDL;
                                TSC_timer_calibration();
                        } else {
                                APIC_timer_calibration();
                        }
                }

                APIC_timer_init(sys_timer_type);
                software_enable_APIC();
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (is_bsp) {
                        if (TSC_DDL_support()) {
                                pr_info("tsc ddl mode is supported\n");
                                sys_timer_type = TIMER_TYPE_X86_TSC_DDL;
                                TSC_timer_calibration();
                        } else {
                                APIC_timer_calibration();
                        }
                }
                APIC_timer_init(sys_timer_type);
                software_enable_APIC();
        }
        if (is_bsp)
                get_rtc_time();
}
void arch_reset_timer(void)
{
        APIC_timer_reset(sys_timer_type);
}