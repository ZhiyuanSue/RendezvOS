#include <shampoos/trap.h>

void (*irq_handler[NR_IRQ])(struct trap_frame *tf);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf))
{
        irq_handler[irq_num] = handler;
}