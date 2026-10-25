#include <rendezvos/trap/irq_space.h>
#include <rendezvos/limits.h>

size_t arch_local_lookup_global_irq_id(cpu_id_t cpu_id, size_t local_irq_id)
{
        size_t hw_irq; /* GIC INTID */

        /*
         * Formula map; cpu_id unused for the math. CPU_ID_INVALID still
         * means "current CPU" for the common calling convention.
         */
        if (cpu_id != CPU_ID_INVALID
            && cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return IRQ_GLOBAL_ID_INVALID;
        if (irq_trap_is_local(local_irq_id))
                return IRQ_GLOBAL_ID_INVALID;
        hw_irq = AARCH64_TRAP_ID_TO_IRQ(local_irq_id);
        if (hw_irq < (size_t)GIC_V2_SPI_START
            || hw_irq > (size_t)GIC_V2_SPI_END)
                return IRQ_GLOBAL_ID_INVALID;
        return hw_irq - (size_t)GIC_V2_SPI_START;
}

size_t arch_choose_local_irq_id(size_t global_irq_id, cpu_id_t cpu_id)
{
        size_t hw_irq; /* GIC INTID */

        (void)cpu_id;
        hw_irq = global_irq_id + (size_t)GIC_V2_SPI_START;
        if (hw_irq > (size_t)GIC_V2_SPI_END)
                return IRQ_LOCAL_ID_INVALID;
        /* Formula: SPI index → INTID → local_irq_id (= intid + 64). */
        size_t local_irq_id = (size_t)AARCH64_IRQ_TO_TRAP_ID(hw_irq);
        return local_irq_id;
}

bool arch_global_bind_local_irq_id(cpu_id_t cpu_id, size_t local_irq_id,
                           size_t global_irq_id)
{
        (void)local_irq_id;
        (void)global_irq_id;
        /* Formula map; no table to update. CPU_ID_INVALID = current CPU. */
        if (cpu_id != CPU_ID_INVALID
            && cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return false;
        return true;
}

void arch_irq_global_entry_reset(struct arch_global_irq *entry)
{
        (void)entry;
}
