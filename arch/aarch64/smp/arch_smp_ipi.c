#include <arch/aarch64/gic/gic_v2.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/trap/trap.h>

void arch_smp_ipi_init(void (*handler)(struct trap_frame *tf))
{
        register_irq_handler(ARCH_IRQ_VEC_IPI, handler, IRQ_NEED_EOI);
}

error_t arch_smp_ipi_send(cpu_id_t cpu)
{
        u32 target_list;

        if (cpu >= GIC_V2_NR_CPU_MAX)
                return -E_IN_PARAM;
        if (!gic.send_sgi)
                return -E_RENDEZVOS;

        if (cpu == percpu(cpu_number)) {
                gic.send_sgi(ARCH_IRQ_INTID_IPI, GIC_V2_GICD_SGIR_TARGET_SELF,
                             0);
                return REND_SUCCESS;
        }

        target_list = (1u << (u32)cpu) << GIC_V2_GICD_SGIR_TARGET_LIST_SHIFT;
        gic.send_sgi(ARCH_IRQ_INTID_IPI, GIC_V2_GICD_SGIR_TARGET_SPECIFIED,
                     target_list);
        return REND_SUCCESS;
}
