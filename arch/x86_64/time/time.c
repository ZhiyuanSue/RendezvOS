#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <shampoos/mm/vmm.h>
#include <common/bit.h>
#include <shampoos/time.h>

extern int arch_irq_type;
// Here we use PIT to calibration
// if possible, use HPET instead
static void PIT_delay(int ms)
{
        init_8254_one_shot((PIT_TICK_RATE * ms) / 1000);
        init_8254_read();
        i16 t = read_8254_val();
        while (t >= 0) {
                t = read_8254_val();
        }
}
static void APIC_timer_calibration()
{
#define APIC_CALIBRATE_MS 50
        /*
                for pic, only 16 bits, and the max is 65535
                so if the tick is 1193181 / 1000 per ms
                we can only count 50 time (59659)  every time
        */
        u32 apic_timer_irq_num = _8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_;
        u32 timer_value = 0;
        u32 timer_count = 0;
        if (arch_irq_type == xAPIC_IRQ) {
                timer_value = xAPIC_RD_REG(LVT_TIME, KERNEL_VIRT_OFFSET);
        } else if (arch_irq_type == x2APIC_IRQ) {
                timer_value = x2APIC_RD_REG(LVT_TIME, KERNEL_VIRT_OFFSET);
        }
        timer_value = clear_mask(timer_value, APIC_LVT_MASKED);
        timer_value = clear_mask(timer_value, APIC_LVT_VECTOR_MASK);
        timer_value = set_mask(timer_value, apic_timer_irq_num);
        // first set to one shot mode
        timer_value = clear_mask(timer_value, APIC_LVT_TIMER_MODE_MASK);
        timer_value = set_mask(timer_value, APIC_LVT_TIMER_MODE_ONE_SHOT);
        if (arch_irq_type == xAPIC_IRQ) {
                xAPIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
                timer_value = set_mask(timer_value, APIC_LVT_MASKED);
                xAPIC_WR_REG(DCR, KERNEL_VIRT_OFFSET, APIC_DCR_DIV_16);
                xAPIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, 0xFFFFFFFF);
        } else if (arch_irq_type == x2APIC_IRQ) {
                x2APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
                timer_value = set_mask(timer_value, APIC_LVT_MASKED);
                x2APIC_WR_REG(DCR, KERNEL_VIRT_OFFSET, APIC_DCR_DIV_16);
                x2APIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, 0xFFFFFFFF);
        }
        PIT_delay(APIC_CALIBRATE_MS);
        if (arch_irq_type == xAPIC_IRQ) {
                xAPIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
                timer_count = xAPIC_RD_REG(CURR_CNT, KERNEL_VIRT_OFFSET);
        } else if (arch_irq_type == x2APIC_IRQ) {
                x2APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
                timer_count = x2APIC_RD_REG(CURR_CNT, KERNEL_VIRT_OFFSET);
        }
        timer_count = -timer_count;
        timer_count = timer_count / (APIC_CALIBRATE_MS / SYS_TIME_MS_PER_INT);
        timer_value = clear_mask(timer_value, APIC_LVT_MASKED);
        timer_value = set_mask(timer_value, APCI_LVT_TIMER_MODE_PERIODIC);
        // TODO: tsc ddl mode, test and set part
        pr_info("timer count is %x\n", timer_count);
        if (arch_irq_type == xAPIC_IRQ) {
                xAPIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, timer_count);
                xAPIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
        } else if (arch_irq_type == x2APIC_IRQ) {
                x2APIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, timer_count);
                x2APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
        }
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
                software_enable_APIC();
        } else if (arch_irq_type == x2APIC_IRQ) {
                APIC_timer_calibration();
                software_enable_APIC();
        }
}