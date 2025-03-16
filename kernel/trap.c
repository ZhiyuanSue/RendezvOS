#include <shampoos/trap.h>
#include <common/stddef.h>
#include <shampoos/percpu.h>

// void (*irq_handler[NR_IRQ])(struct trap_frame *tf);
DEFINE_PER_CPU(void (*[NR_IRQ])(struct trap_frame *tf), irq_handler);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf))
{
        percpu(irq_handler[irq_num]) = handler;
}
void trap_handler(struct trap_frame *tf)
{
        u64 trap_id = TRAP_ID((tf->trap_info));
        if (percpu(irq_handler[trap_id])) {
                percpu(irq_handler[trap_id])(tf);
        } else {
                arch_unknown_trap_handler(tf);
        }
}
void init_interrupt()
{
        for (int i = 0; i < NR_IRQ; i++) {
                percpu(irq_handler[i]) = NULL;
        }
        arch_init_interrupt();
}