#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/time.h>
#include <arch/x86_64/trap/trap.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/time.h>

extern enum IRQ_type arch_irq_type;
extern enum timer_type sys_timer_type;

u32 arch_get_timer_irq_num(bool is_bsp)
{
        (void)is_bsp;
        return ARCH_IRQ_VEC_TIMER;
}

/**
 * @brief x86 timer bring-up by arch_irq_type (PIC / xAPIC / x2APIC).
 *
 * PIC: BSP only programs 8254 one-shot + enable IRQ0→0x20.
 * x(2)APIC: 
 * - BSP calibrates (TSC-DDL if supported else LAPIC)
 * - APIC_timer_init
 * - software_enable_APIC to start the timer irq
 * @return heartbeat gap.
 */
u64 arch_init_timer(bool is_bsp)
{
        u64 heartbeat_gap = 0;
        if (arch_irq_type == NO_IRQ) {
                return 0;
        } else if (arch_irq_type == PIC_IRQ) {
                heartbeat_gap = PIT_TICK_RATE / INT_PER_SECOND;
                if (is_bsp) {
                        init_8254_one_shot(heartbeat_gap);
                        PIT_update_timer((u16)heartbeat_gap);
                        enable_PIC_IRQ(arch_get_timer_irq_num(is_bsp));
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

                heartbeat_gap = APIC_timer_init(sys_timer_type);
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
                heartbeat_gap = APIC_timer_init(sys_timer_type);
                software_enable_APIC();
        }

        return heartbeat_gap;
}
void arch_reset_timer(u64 next_event_gap)
{
        if (arch_irq_type != PIC_IRQ) {
                APIC_timer_reset(sys_timer_type, next_event_gap);
        } else if (arch_irq_type == PIC_IRQ) {
                if (percpu(cpu_number) != BSP_ID)
                        return;
                /*for smp, it must use apic, so if pic mode, it must be bsp*/
                init_8254_one_shot(next_event_gap);
                PIT_update_timer((u16)next_event_gap);
        }
}
tick_t arch_timer_read(void)
{
        if (arch_irq_type != PIC_IRQ) {
                return APIC_timer_read(sys_timer_type);
        } else if (arch_irq_type == PIC_IRQ) {
                return PIT_timer_read();
        }
        return 0;
}
tick_t arch_timer_get_hz(void)
{
        if (arch_irq_type != PIC_IRQ) {
                return APIC_timer_hz(sys_timer_type);
        } else if (arch_irq_type == PIC_IRQ) {
                return PIT_get_hz();
        }
        return 0;
}