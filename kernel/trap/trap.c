#include <modules/log/log.h>
#include <common/stddef.h>
#include <common/atomic.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/common.h>
#include <rendezvos/system/panic.h>

extern Task_Manager *core_tm;

DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

/*
 * Filled by @c irq_global_vector_alloc during @c arch_start_platform.
 * Entry ownership is CAS on @c irq_attr (set @c IRQ_ATTR_USED for a given
 * global_irq_id). No vector-wide lock; failed claim returns an error.
 */
struct global_irq *global_irq_vector;
size_t nr_global_irq;

bool irq_trap_is_local(size_t local_irq_id)
{
        return local_irq_id < (size_t)NR_ARCH_LOCAL_IRQ;
}

struct global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id)
{
        size_t global_irq_id = arch_map_global_irq_id(cpu_id, local_irq_id);
        if (global_irq_id >= nr_global_irq)
                return NULL;
        struct global_irq *entry = &global_irq_vector[global_irq_id];

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
        size_t i;

        if (!count)
                return -E_IN_PARAM;
        if (global_irq_vector)
                return -E_RENDEZVOS;

        struct allocator *cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_alloc)
                return -E_RENDEZVOS;

        struct global_irq *vector = cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(struct global_irq) * count);
        if (!vector)
                return -E_RENDEZVOS;
        for (i = 0; i < count; i++) {
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
        struct global_irq *global_entry;
        bool mapped;

        if (!local_irq_id_out || global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        local_irq_id = arch_map_fixed_local_irq_id(global_irq_id);
        mapped = false;

        if (local_irq_id != IRQ_LOCAL_ID_INVALID) {
                /*
                 * Fixed local_irq_id: require it still free to claim, then
                 * claim the global entry, then write the map.
                 */
                if (!arch_local_irq_id_available(cpu_id, local_irq_id))
                        return -E_REND_AGAIN;
                if (!irq_atomic_attr_try_set_used(&global_entry->irq_attr))
                        return -E_REND_AGAIN;
                if (!arch_map_local_irq_id(cpu_id, local_irq_id, global_irq_id))
                        goto alloc_fail;
                mapped = true;
        } else {
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
        struct global_irq *global_entry;

        if (global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        if (!irq_atomic_attr_is_used(&global_entry->irq_attr)
            || global_entry->cpu_id != cpu_id)
                return -E_REND_NOFOUND;

        (void)arch_map_local_irq_id(
                cpu_id, global_entry->local_irq_id, IRQ_GLOBAL_ID_INVALID);
        global_entry->local_irq_id = IRQ_LOCAL_ID_INVALID;
        global_entry->cpu_id = CPU_ID_INVALID;
        global_entry->irq_handler = NULL;
        arch_irq_global_entry_reset(global_entry);
        /* Clear USED last so a lookup that already read the map stops here. */
        irq_atomic_attr_store(&global_entry->irq_attr, IRQ_NO_ATTR);
        return REND_SUCCESS;
}

void register_irq_handler(size_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr)
{
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER) {
                pr_error("[trap] register_irq_handler: bad cpu %u\n",
                         (u32)cpu_id);
                return;
        }

        if (irq_trap_is_local(local_irq_id)) {
                struct local_irq *local_entry =
                        &per_cpu(local_irq_vector[local_irq_id], cpu_id);
                local_entry->irq_handler = handler;
                local_entry->irq_attr = irq_attr_set_used(irq_attr);
        } else {
                struct global_irq *global_entry =
                        irq_global_find(local_irq_id, cpu_id);

                if (!global_entry) {
                        pr_error(
                                "[trap] register_irq_handler: local_irq_id %zu cpu %u not allocated\n",
                                local_irq_id,
                                (u32)cpu_id);
                        return;
                }
                global_entry->irq_handler = handler;
                irq_atomic_attr_store(&global_entry->irq_attr,
                                      irq_attr_set_used(irq_attr));
        }
}

void trap_handler(struct trap_frame *tf)
{
        size_t local_irq_id = (size_t)TRAP_ID((tf->trap_info));
        void (*handler)(struct trap_frame *) = NULL;
        u64 attr = IRQ_NO_ATTR;

        if (irq_trap_is_local(local_irq_id)) {
                handler = percpu(local_irq_vector[local_irq_id].irq_handler);
                attr = percpu(local_irq_vector[local_irq_id].irq_attr);
        } else {
                struct global_irq *global_entry =
                        irq_global_find(local_irq_id, CPU_ID_INVALID);

                if (global_entry) {
                        handler = global_entry->irq_handler;
                        attr = irq_atomic_attr_load(&global_entry->irq_attr);
                }
        }

        if (handler) {
                handler(tf);
        } else {
                arch_unknown_trap_handler(tf);
                kernel_panic("Unhandled trap: unknown IRQ");
        }
        if (irq_attr_need_eoi(attr))
                arch_eoi_irq(tf->trap_info);
        if (!arch_int_from_kernel(tf) && percpu(core_tm))
                schedule(percpu(core_tm));
}

void init_interrupt(void)
{
        if (!global_irq_vector || !nr_global_irq)
                kernel_panic("init_interrupt: global irq vector not allocated");
        arch_init_interrupt();
}
