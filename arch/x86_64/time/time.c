#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>
#include <shampoos/mm/vmm.h>
#include <common/bit.h>
#include <shampoos/time.h>

extern enum IRQ_type arch_irq_type;
void arch_init_timer(void)
{
        if (arch_irq_type == NO_IRQ) {
                return;
        } else if (arch_irq_type == PIC_IRQ) {
                init_8254_cyclical(1000);
                enable_PIC_IRQ(_8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_);
        } else if (arch_irq_type == xAPIC_IRQ) {
                if (TSC_DDL_support()) {
                        pr_info("tsc ddl mode is supported\n");
                        /*
                                TODO: but not supported in qemu platform
                                and if we using tsc ddl mode
                                the apic timer regs are no use anymore
                        */
                }
                APIC_timer_calibration();
                APIC_timer_reset();
                software_enable_APIC();
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (TSC_DDL_support()) {
                        pr_info("tsc ddl mode is supported\n");
                        /*
                                same TODO
                        */
                }
                APIC_timer_calibration();
                APIC_timer_reset();
                software_enable_APIC();
        }
        get_rtc_time();
}