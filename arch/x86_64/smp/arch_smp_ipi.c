#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/trap/trap.h>

extern enum IRQ_type arch_irq_type;

#define ARCH_SMP_IPI_VECTOR 0x30u

void arch_smp_ipi_init(void (*handler)(struct trap_frame *tf))
{
        register_irq_handler(ARCH_SMP_IPI_VECTOR, handler, IRQ_NEED_EOI);
}

error_t arch_smp_ipi_send(cpu_id_t cpu)
{
        if (arch_irq_type != xAPIC_IRQ && arch_irq_type != x2APIC_IRQ)
                return -E_RENDEZVOS;

        APIC_send_IPI((u8)cpu,
                      APIC_ICR_DEST_SH_NO,
                      APIC_ICR_TRIGGER_EDGE,
                      APIC_ICR_LEVEL_ASSERT,
                      APIC_ICR_DEST_PHYSICAL,
                      APIC_ICR_DEL_MODE_FIXED,
                      ARCH_SMP_IPI_VECTOR);
        return REND_SUCCESS;
}
