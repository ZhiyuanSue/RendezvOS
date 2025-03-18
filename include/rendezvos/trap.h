#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_
#ifdef _AARCH64_
#include <arch/aarch64/trap/trap.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/trap/trap.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/trap/trap.h>
#endif

struct irq {
        void (*irq_handler)(struct trap_frame *tf);
#define IRQ_NEED_EOI (1)
        u64 irq_attr;
};
extern struct irq irq_handler[NR_IRQ];
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr);
void init_interrupt();
void arch_eoi_irq(u64 trap_info);
#endif