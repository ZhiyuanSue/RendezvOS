#ifdef _X86_64_
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>

extern int arch_irq_type;
// Here we use PIT to calibration
// if possible, use HPET instead
static void PIT_delay(int ms)
{
        count = 0;
        init_8254_one_shot(PIT_TICK_RATE / (1000 / ms));
        init_8254_read();
        i16 t = read_8254_val();
        while (t >= 0) {
                t = read_8254_val();
        }
}
static void APIC_timer_calibration()
{
        PIT_delay(50);
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