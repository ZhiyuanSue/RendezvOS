#ifndef _RENDEZVOS_ARCH_IRQ_H_
#define _RENDEZVOS_ARCH_IRQ_H_
#include <arch/x86_64/PIC/PIC.h>
#include <arch/x86_64/PIC/APIC.h>

/**
 * @brief Selected interrupt controller for this boot (set once by init_irq).
 */
enum IRQ_type {
        NO_IRQ,
        PIC_IRQ, /** Dual 8259A*/
        xAPIC_IRQ, /** Local xAPIC via MMIO 0xFEE00000*/
        x2APIC_IRQ, /** Local x2APIC via MSR*/
};

/**
 * @brief PIC / xAPIC / x2APIC bring-up 
 */
void init_irq(void);

#endif