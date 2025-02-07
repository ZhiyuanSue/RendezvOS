#ifdef _AARCH64_
#include <arch/aarch64/trap/trap.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/trap/trap.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/trap/trap.h>
#endif

extern void (*irq_handler[NR_IRQ])(struct trap_frame *tf);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf));
void init_interrupt();