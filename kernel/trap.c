#include <rendezvos/trap.h>
#include <common/stddef.h>
#include <rendezvos/percpu.h>
#include <rendezvos/task/tcb.h>
#include <modules/log/log.h>
// void (*irq_handler[NR_IRQ])(struct trap_frame *tf);
extern Task_Manager *core_tm;
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr)
{
        percpu(irq_vector[irq_num].irq_handler) = handler;
        percpu(irq_vector[irq_num].irq_attr) = irq_attr;
}
void trap_handler(struct trap_frame *tf)
{
        u64 trap_id = TRAP_ID((tf->trap_info));
        if (percpu(irq_vector[trap_id].irq_handler)) {
                percpu(irq_vector[trap_id].irq_handler)(tf);
        } else {
                arch_unknown_trap_handler(tf);
        }
        if (percpu(irq_vector[trap_id].irq_attr) & IRQ_NEED_EOI) {
                arch_eoi_irq(tf->trap_info);
        }
        if (!arch_int_from_kernel(tf)) {
                pr_info("user int and schedule\n");
                if (percpu(core_tm) && percpu(core_tm)->schedule) {
                        percpu(core_tm)->schedule(percpu(core_tm));
                }
        }
}
void init_interrupt()
{
        for (int i = 0; i < NR_IRQ; i++) {
                percpu(irq_vector[i].irq_handler) = NULL;
        }
        arch_init_interrupt();
}