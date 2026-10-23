#include <modules/log/log.h>
#include <common/stddef.h>
#include <common/atomic.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/trap/irq_space.h>
#include <rendezvos/common.h>

DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

/*
 * Global vector: filled once by @c irq_global_vector_alloc on the BSP.
 * Ownership rules: see @c irq_space.h. This file does not touch hardware
 * controllers — only the software tables and maps.
 */
struct arch_global_irq *global_irq_vector;
size_t nr_global_irq;

bool irq_trap_is_local(size_t local_irq_id)
{
        return local_irq_id < (size_t)NR_ARCH_LOCAL_IRQ;
}

struct arch_global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id)
{
        size_t global_irq_id = arch_map_global_irq_id(cpu_id, local_irq_id);
        if (global_irq_id >= nr_global_irq)
                return NULL;
        struct arch_global_irq *entry = &global_irq_vector[global_irq_id];

        cpu_id_t owner_cpu = cpu_id;
        if (owner_cpu == CPU_ID_INVALID)
                owner_cpu = percpu(cpu_number);

        if (!irq_atomic_attr_is_used(&entry->irq_attr)
            || entry->local_irq_id != local_irq_id
            || entry->cpu_id != owner_cpu)
                return NULL;
        return entry;
}

error_t irq_global_vector_alloc(size_t count)
{
        if (!count)
                return -E_IN_PARAM;
        if (global_irq_vector)
                return -E_RENDEZVOS;

        struct allocator *cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_alloc)
                return -E_RENDEZVOS;

        struct arch_global_irq *vector = cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(struct arch_global_irq) * count);
        if (!vector)
                return -E_RENDEZVOS;
        for (size_t i = 0; i < count; i++) {
                atomic64_init(&vector[i].irq_attr, 0);
                vector[i].local_irq_id = IRQ_LOCAL_ID_INVALID;
                vector[i].cpu_id = CPU_ID_INVALID;
                vector[i].irq_handler = NULL;
                arch_irq_global_entry_reset(&vector[i]);
        }
        global_irq_vector = vector;
        nr_global_irq = count;
        return REND_SUCCESS;
}

error_t irq_global_alloc(size_t global_irq_id, cpu_id_t cpu_id,
                         size_t *local_irq_id_out)
{
        size_t local_irq_id;
        size_t candidate;
        struct arch_global_irq *global_entry;

        if (!local_irq_id_out || global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        /* Prefer arch direct map; INVALID → pick a free local_irq_id below. */
        local_irq_id = arch_try_direct_map_local_irq_id(global_irq_id);

        if (local_irq_id != IRQ_LOCAL_ID_INVALID) {
                /*
                 * Direct map accepted: require that local_irq_id still free,
                 * claim the global entry, then write the map.
                 */
                if (!arch_local_irq_id_available(cpu_id, local_irq_id))
                        return -E_REND_AGAIN;
                if (!irq_atomic_attr_try_set_used(&global_entry->irq_attr))
                        return -E_REND_AGAIN;
                if (!arch_map_local_irq_id(cpu_id, local_irq_id, global_irq_id))
                        goto alloc_fail;
        } else {
                bool mapped = false;

                /* No direct map (e.g. x86): scan [ALLOC_LO, ALLOC_HI]. */
                if (!irq_atomic_attr_try_set_used(&global_entry->irq_attr))
                        return -E_REND_AGAIN;
                for (candidate = ARCH_IRQ_ID_ALLOC_LO;
                     candidate <= ARCH_IRQ_ID_ALLOC_HI;
                     candidate++) {
                        if (!arch_local_irq_id_available(cpu_id, candidate))
                                continue;
                        if (!arch_map_local_irq_id(
                                    cpu_id, candidate, global_irq_id))
                                continue;
                        local_irq_id = candidate;
                        mapped = true;
                        break;
                }
                if (!mapped) {
                        (void)irq_atomic_attr_try_set_unused(
                                &global_entry->irq_attr);
                        return -E_REND_OVERFLOW;
                }
        }

        global_entry->local_irq_id = local_irq_id;
        global_entry->cpu_id = cpu_id;
        *local_irq_id_out = local_irq_id;
        return REND_SUCCESS;

alloc_fail:
        (void)irq_atomic_attr_try_set_unused(&global_entry->irq_attr);
        return -E_REND_AGAIN;
}

error_t irq_global_free(size_t global_irq_id, cpu_id_t cpu_id)
{
        struct arch_global_irq *global_entry;

        if (global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;
        /*
         * Only the owning CPU may tear down the entry. A remote free could
         * race with that CPU's trap_handler / register path that already
         * obtained the entry pointer after seeing USED; USED is not a lease.
         */
        if (percpu(cpu_number) != cpu_id)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        if (!irq_atomic_attr_is_used(&global_entry->irq_attr)
            || global_entry->cpu_id != cpu_id)
                return -E_REND_NOFOUND;

        (void)arch_map_local_irq_id(
                cpu_id, global_entry->local_irq_id, IRQ_GLOBAL_ID_INVALID);
        global_entry->local_irq_id = IRQ_LOCAL_ID_INVALID;
        global_entry->cpu_id = CPU_ID_INVALID;
        /* Keep irq_handler and sticky attr bits (e.g. NEED_EOI) for migrate. */
        arch_irq_global_entry_reset(global_entry);
        /*
         * Drop USED last so find that already resolved the map fails the
         * used/affinity check. Do not store IRQ_NO_ATTR — that would wipe
         * NEED_EOI and force a re-register after every free.
         */
        (void)irq_atomic_attr_try_set_unused(&global_entry->irq_attr);
        return REND_SUCCESS;
}
