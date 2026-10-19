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
 * Filled by @c irq_global_table_alloc during @c arch_start_platform.
 * Slot ownership is CAS on @c irq_attr (0 → USED); losers skip to the
 * next slot — no table-wide lock.
 */
struct global_irq *global_irq_vector;
size_t nr_global_irq;

bool irq_trap_is_local(size_t local_irq_id)
{
        return local_irq_id < (size_t)NR_ARCH_LOCAL_IRQ;
}

struct global_irq *irq_global_from_local_id(size_t local_irq_id)
{
        size_t global_irq_id = arch_map_global_irq_id(local_irq_id);

        if (global_irq_id >= nr_global_irq)
                return NULL;
        return &global_irq_vector[global_irq_id];
}

struct global_irq *irq_global_find(size_t local_irq_id, cpu_id_t dest_cpu)
{
        size_t global_irq_id;
        struct global_irq *entry;

        global_irq_id = arch_map_global_irq_id_on_cpu(dest_cpu,
                                                                  local_irq_id);
        if (global_irq_id >= nr_global_irq)
                return NULL;
        entry = &global_irq_vector[global_irq_id];
        if (!irq_global_attr_is_used(&entry->irq_attr)
            || entry->local_irq_id != local_irq_id
            || entry->dest_cpu != dest_cpu)
                return NULL;
        return entry;
}

error_t irq_global_table_alloc(size_t count)
{
        struct allocator *cpu_kallocator;
        struct global_irq *table;
        size_t i;

        if (!count)
                return -E_IN_PARAM;
        if (global_irq_vector)
                return -E_RENDEZVOS;

        cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_alloc)
                return -E_RENDEZVOS;

        table = cpu_kallocator->m_alloc(cpu_kallocator,
                                        sizeof(struct global_irq) * count);
        if (!table)
                return -E_RENDEZVOS;
        for (i = 0; i < count; i++)
                atomic64_init(&table[i].irq_attr, 0);
        global_irq_vector = table;
        nr_global_irq = count;
        return REND_SUCCESS;
}

error_t irq_global_alloc(size_t global_irq_id, cpu_id_t dest_cpu,
                         size_t *local_irq_id_out)
{
        size_t local_irq_id;
        size_t candidate;
        struct global_irq *global_entry;
        bool mapped;

        if (!local_irq_id_out || global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        if (!irq_global_attr_try_claim(&global_entry->irq_attr))
                return -E_RENDEZVOS;

        mapped = false;
        local_irq_id = arch_map_fixed_local_irq_id(global_irq_id);
        if (local_irq_id != IRQ_LOCAL_ID_INVALID) {
                mapped = arch_local_irq_id_available(dest_cpu, local_irq_id)
                         && arch_map_local_irq_id(dest_cpu, local_irq_id,
                                                  global_irq_id);
        } else {
                for (candidate = ARCH_IRQ_VEC_ALLOC_LO;
                     candidate <= ARCH_IRQ_VEC_ALLOC_HI; candidate++) {
                        if (!arch_local_irq_id_available(dest_cpu, candidate))
                                continue;
                        if (!arch_map_local_irq_id(dest_cpu, candidate,
                                                   global_irq_id))
                                continue;
                        local_irq_id = candidate;
                        mapped = true;
                        break;
                }
        }
        if (!mapped) {
                irq_global_attr_abort_claim(&global_entry->irq_attr);
                return -E_RENDEZVOS;
        }

        global_entry->local_irq_id = local_irq_id;
        global_entry->dest_cpu = dest_cpu;
        *local_irq_id_out = local_irq_id;
        return REND_SUCCESS;
}

error_t irq_global_free(size_t global_irq_id, cpu_id_t dest_cpu)
{
        struct global_irq *global_entry;

        if (global_irq_id >= nr_global_irq)
                return -E_IN_PARAM;
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = &global_irq_vector[global_irq_id];
        if (!irq_global_attr_is_used(&global_entry->irq_attr)
            || global_entry->dest_cpu != dest_cpu)
                return -E_RENDEZVOS;

        (void)arch_map_local_irq_id(dest_cpu, global_entry->local_irq_id,
                                    IRQ_GLOBAL_ID_INVALID);
        global_entry->local_irq_id = 0;
        global_entry->dest_cpu = 0;
        global_entry->irq_handler = NULL;
        /* Clear USED last so a lookup that already read the map stops here. */
        irq_global_attr_store(&global_entry->irq_attr, IRQ_NO_ATTR);
        return REND_SUCCESS;
}

void register_irq_handler(size_t local_irq_id, cpu_id_t dest_cpu,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr)
{
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER) {
                pr_error("[trap] register_irq_handler: bad cpu %u\n",
                         (u32)dest_cpu);
                return;
        }

        if (irq_trap_is_local(local_irq_id)) {
                struct local_irq *local_entry =
                        &per_cpu(local_irq_vector[local_irq_id], dest_cpu);
                local_entry->irq_handler = handler;
                local_entry->irq_attr = irq_attr_set_used(irq_attr);
                return;
        } else {
                struct global_irq *global_entry =
                        irq_global_find(local_irq_id, dest_cpu);
                if (!global_entry
                    || !irq_global_attr_is_used(&global_entry->irq_attr)) {
                        pr_error(
                                "[trap] register_irq_handler: local_irq_id %zu cpu %u not allocated\n",
                                local_irq_id,
                                (u32)dest_cpu);
                        return;
                }
                global_entry->irq_handler = handler;
                irq_global_attr_store(&global_entry->irq_attr,
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
                        irq_global_from_local_id(local_irq_id);

                if (global_entry) {
                        handler = global_entry->irq_handler;
                        attr = irq_global_attr_load(&global_entry->irq_attr);
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
                kernel_panic("init_interrupt: global irq table not allocated");
        arch_init_interrupt();
}
