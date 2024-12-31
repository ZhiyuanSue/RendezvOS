#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <modules/log/log.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <shampoos/mm/vmm.h>
#include <common/bit.h>
#include <shampoos/time.h>

extern enum IRQ_type arch_irq_type;
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
static tick_t APIC_timer_calibration()
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

        timer_value = set_mask(timer_value, apic_timer_irq_num);
        // first set to one shot mode
        timer_value = set_mask(timer_value, APIC_LVT_TIMER_MODE_ONE_SHOT);
        APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
        timer_value = set_mask(timer_value, APIC_LVT_MASKED);
        APIC_WR_REG(DCR, KERNEL_VIRT_OFFSET, APIC_DCR_DIV_16);
        APIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, 0xFFFFFFFF);
        PIT_delay(APIC_CALIBRATE_MS);
        APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, timer_value);
        timer_count = APIC_RD_REG(CURR_CNT, KERNEL_VIRT_OFFSET);
        timer_count = -timer_count;
        timer_count = timer_count / (APIC_CALIBRATE_MS / SYS_TIME_MS_PER_INT);
        // TODO: tsc ddl mode, test and set part
        pr_info("timer count is %x\n", timer_count);
        return timer_count;
}
void APIC_timer_init(u32 init_cnt)
{
        u32 lvt_timer_val = 0;
        u32 apic_timer_irq_num = _8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_;
        lvt_timer_val = set_mask(lvt_timer_val, apic_timer_irq_num);
        lvt_timer_val = set_mask(lvt_timer_val, APCI_LVT_TIMER_MODE_PERIODIC);

        APIC_WR_REG(INIT_CNT, KERNEL_VIRT_OFFSET, init_cnt);
        APIC_WR_REG(DCR, KERNEL_VIRT_OFFSET, APIC_DCR_DIV_16);
        APIC_WR_REG(LVT_TIME, KERNEL_VIRT_OFFSET, lvt_timer_val);
}
void init_timer(void)
{
        if (arch_irq_type == NO_IRQ) {
                return;
        } else if (arch_irq_type == PIC_IRQ) {
                init_8254_cyclical(1000);
                enable_IRQ(_8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_);
        } else if (arch_irq_type == xAPIC_IRQ) {
                if (TSC_DDL_support()) {
                        pr_info("tsc ddl mode is supported\n");
                        /*
                                TODO: but not supported in qemu platform
                                and if we using tsc ddl mode
                                the apic timer regs are no use anymore
                        */
                }
                u64 apic_hz_per_second = APIC_timer_calibration();
                // TODO:calculate the apic hz and calculate the init_cnt to
                // write in
                APIC_timer_init((u32)apic_hz_per_second);
                software_enable_APIC();
        } else if (arch_irq_type == x2APIC_IRQ) {
                if (TSC_DDL_support()) {
                        pr_info("tsc ddl mode is supported\n");
                        /*
                                same TODO
                        */
                }
                APIC_timer_calibration();
                software_enable_APIC();
        }
}