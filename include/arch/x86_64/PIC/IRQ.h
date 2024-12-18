#ifndef _SHAMPOOS_ARCH_IRQ_H_
#define _SHAMPOOS_ARCH_IRQ_H_
#include <arch/x86_64/PIC/PIT.h>
enum IRQ_type {
        NO_IRQ,
        PIC_IRQ,
        xAPIC_IRQ,
        x2APIC_IRQ,
};
void init_irq(void);

#endif