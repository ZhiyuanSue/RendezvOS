#ifndef _RENDEZVOS_ARCH_IRQ_H_
#define _RENDEZVOS_ARCH_IRQ_H_
#include <arch/x86_64/PIC/PIC.h>
#include <arch/x86_64/PIC/APIC.h>
enum IRQ_type {
        NO_IRQ,
        PIC_IRQ,
        xAPIC_IRQ,
        x2APIC_IRQ,
};
void init_irq(void);

#endif