#include <rendezvos/trap/irq_space.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/limits.h>
#include <common/atomic.h>
#include <common/limits.h>

/*
 * Per-CPU map: local_irq_id → global_irq_id + 1 (0 = not mapped).
 * Stored as u64 for CAS; see arch/x86_64/trap/irq_space.h.
 */
DEFINE_PER_CPU(u64, local_irq_map[NR_IDT_VECTORS]);

global_irq_id_t arch_local_lookup_global_irq_id(cpu_id_t cpu_id,
                                                local_irq_id_t local_irq_id)
{
        u64 map_value;

        if (local_irq_id >= (local_irq_id_t)NR_IDT_VECTORS)
                return IRQ_GLOBAL_ID_INVALID;
        /* CPU_ID_INVALID: query the current CPU's map (percpu). */
        if (cpu_id == CPU_ID_INVALID) {
                map_value = percpu(local_irq_map[local_irq_id]);
        } else if (cpu_id < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER) {
                map_value = per_cpu(local_irq_map[local_irq_id], cpu_id);
        } else {
                return IRQ_GLOBAL_ID_INVALID;
        }
        return map_value ? (global_irq_id_t)(map_value - 1u) :
                           IRQ_GLOBAL_ID_INVALID;
}

local_irq_id_t arch_choose_local_irq_id(global_irq_id_t global_irq_id,
                                        cpu_id_t cpu_id)
{
        (void)global_irq_id;
        if (cpu_id != CPU_ID_INVALID
            && cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return IRQ_LOCAL_ID_INVALID;
        /* Scan alloc window for a local id not yet mapped on cpu_id. */
        for (local_irq_id_t candidate = ARCH_IRQ_ID_ALLOC_LO;
             candidate <= ARCH_IRQ_ID_ALLOC_HI;
             candidate++) {
                if (arch_local_lookup_global_irq_id(cpu_id, candidate)
                    == IRQ_GLOBAL_ID_INVALID)
                        return candidate;
        }
        return IRQ_LOCAL_ID_INVALID;
}

bool arch_global_bind_local_irq_id(cpu_id_t cpu_id, local_irq_id_t local_irq_id,
                                   global_irq_id_t global_irq_id)
{
        volatile u64 *cell;

        if (local_irq_id >= (local_irq_id_t)NR_IDT_VECTORS)
                return false;
        /* CPU_ID_INVALID: update the current CPU's map (percpu). */
        if (cpu_id == CPU_ID_INVALID)
                cell = &percpu(local_irq_map[local_irq_id]);
        else if (cpu_id < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                cell = &per_cpu(local_irq_map[local_irq_id], cpu_id);
        else
                return false;
        /* IRQ_GLOBAL_ID_INVALID: clean the map */
        if (global_irq_id == IRQ_GLOBAL_ID_INVALID) {
                atomic64_store(cell, 0);
                return true;
        }
        if (!nr_global_irq || global_irq_id >= (global_irq_id_t)nr_global_irq)
                return false;
        /*
         * CAS not-mapped(0) → global_irq_id+1. Two GSIs can still race for the
         * same local vector on this CPU, so using CAS to decide who can get
         * this local_irq_map slot.
         */
        return atomic64_cas(cell, 0, (u64)global_irq_id + 1u) == 0;
}

void arch_irq_global_entry_reset(struct arch_global_irq *entry)
{
        if (!entry)
                return;
        entry->ioapic_id = U8_MAX;
        entry->pin = U8_MAX;
}
