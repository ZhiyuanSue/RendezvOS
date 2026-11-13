#include <rendezvos/trap/irq_space.h>
#include <rendezvos/smp/percpu.h>
#include <common/atomic.h>

/*
 * Shared IDT delivery map: local_irq_id → arch_global_irq * (all x86 spaces).
 */
DEFINE_PER_CPU(u64, local_irq_map[NR_IDT_VECTORS]);

/*
 * Callers (irq_global_find / alloc→choose|bind) pass a concrete cpu_id already
 * validated; only index bounds are checked here.
 */
static volatile u64 *x86_local_map_cell(cpu_id_t cpu_id,
                                        local_irq_id_t local_irq_id)
{
        if (local_irq_id >= (local_irq_id_t)NR_IDT_VECTORS)
                return NULL;
        return &per_cpu(local_irq_map[local_irq_id], cpu_id);
}

struct arch_global_irq *arch_irq_entry_from_local(cpu_id_t cpu_id,
                                                  local_irq_id_t local_irq_id)
{
        volatile u64 *cell = x86_local_map_cell(cpu_id, local_irq_id);

        if (!cell)
                return NULL;
        return (struct arch_global_irq *)(uintptr_t)atomic64_load(cell);
}

local_irq_id_t arch_irq_space_choose_local(struct irq_space *space,
                                           global_irq_id_t global_irq_id,
                                           cpu_id_t cpu_id)
{
        (void)space;
        (void)global_irq_id;
        for (local_irq_id_t candidate = ARCH_IRQ_ID_ALLOC_LO;
             candidate <= ARCH_IRQ_ID_ALLOC_HI;
             candidate++) {
                if (!arch_irq_entry_from_local(cpu_id, candidate))
                        return candidate;
        }
        return IRQ_LOCAL_ID_INVALID;
}

bool arch_irq_space_bind_local(struct irq_space *space, cpu_id_t cpu_id,
                               local_irq_id_t local_irq_id,
                               struct arch_global_irq *entry)
{
        volatile u64 *cell = x86_local_map_cell(cpu_id, local_irq_id);

        (void)space;
        if (!cell)
                return false;
        if (!entry) {
                atomic64_store(cell, 0);
                return true;
        }
        return atomic64_cas(cell, 0, (u64)(uintptr_t)entry) == 0;
}
