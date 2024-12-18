#ifdef _X86_64_
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>

extern int arch_irq_type;
static void APIC_timer_calibration()
{
        init_8254_one_shot(PIT_TICK_RATE / 20);
        init_8254_read();
        // int original = read_8254_val();
        int i=0;
        for (; i < 10000; i++) {
                if (read_8254_val()==0)
                        break;
        }
        pr_info("here count 0 with i %d\n",i);
}
void init_timer(void)
{
        if (arch_irq_type == NO_IRQ) {
                return;
        } else if (arch_irq_type == PIC_IRQ) {
                init_8254_cyclical(1000);
                enable_IRQ(_8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_);
        } else if (arch_irq_type == xAPIC_IRQ) {
                APIC_timer_calibration();
        } else if (arch_irq_type == x2APIC_IRQ) {
                ;
        }
}
#endif