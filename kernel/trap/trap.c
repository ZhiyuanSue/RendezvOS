#include <modules/log/log.h>
#include <common/stddef.h>
#include <common/string.h>
#include <common/atomic.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/common.h>
#include <rendezvos/system/panic.h>

extern Task_Manager *core_tm;

DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

/*
 * Softcap storage. Slot ownership is CAS on @c irq_attr (0 → USED);
 * losers skip to the next slot — no table-wide lock.
 */
static struct global_irq global_irq_storage[NR_ARCH_GLOBAL_IRQ];
struct global_irq *global_irq_table;
u32 nr_global_irq;

bool irq_trap_is_local(u32 delivery_id)
{
        return delivery_id < (u32)NR_ARCH_LOCAL_IRQ;
}

struct global_irq *irq_global_from_delivery(u32 delivery_id)
{
        u32 table_index = arch_irq_delivery_to_table_index(delivery_id);

        if (!global_irq_table || table_index == IRQ_TABLE_INDEX_INVALID
            || table_index >= nr_global_irq)
                return NULL;
        return &global_irq_table[table_index];
}

struct global_irq *irq_global_find(u32 delivery_id, cpu_id_t dest_cpu)
{
        u32 slot;

        if (!global_irq_table || !nr_global_irq)
                return NULL;
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return NULL;
        for (slot = 0; slot < nr_global_irq; slot++) {
                struct global_irq *entry = &global_irq_table[slot];

                if (irq_global_attr_is_used(&entry->irq_attr)
                    && entry->delivery_id == delivery_id
                    && entry->dest_cpu == dest_cpu)
                        return entry;
        }
        return NULL;
}

error_t irq_global_table_init(u32 count)
{
        u32 slot;

        if (!count)
                count = (u32)NR_ARCH_GLOBAL_IRQ;
        if (count > (u32)NR_ARCH_GLOBAL_IRQ)
                return -E_IN_PARAM;

        if (global_irq_table) {
                if (count > nr_global_irq)
                        return -E_RENDEZVOS;
                nr_global_irq = count;
                return REND_SUCCESS;
        }

        memset(global_irq_storage, 0, sizeof(global_irq_storage));
        for (slot = 0; slot < (u32)NR_ARCH_GLOBAL_IRQ; slot++)
                atomic64_init(&global_irq_storage[slot].irq_attr, 0);
        global_irq_table = global_irq_storage;
        nr_global_irq = count;
        return REND_SUCCESS;
}

error_t irq_delivery_alloc(u32 *delivery_id_out, cpu_id_t dest_cpu)
{
        u32 delivery_id;
        u32 table_index;
        u32 slot_lo;
        u32 slot_hi;
        u32 slot;
        struct global_irq *global_entry;

        if (!delivery_id_out || !global_irq_table || !nr_global_irq)
                return -E_IN_PARAM;
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        for (delivery_id = ARCH_IRQ_VEC_ALLOC_LO;
             delivery_id <= ARCH_IRQ_VEC_ALLOC_HI; delivery_id++) {
                if (!arch_irq_delivery_available(dest_cpu, delivery_id))
                        continue;

                table_index = arch_irq_fixed_table_index(delivery_id);
                if (table_index == IRQ_TABLE_INDEX_INVALID) {
                        slot_lo = 0;
                        slot_hi = nr_global_irq - 1u;
                } else {
                        if (table_index >= nr_global_irq)
                                continue;
                        slot_lo = table_index;
                        slot_hi = table_index;
                }

                for (slot = slot_lo; slot <= slot_hi; slot++) {
                        global_entry = &global_irq_table[slot];
                        if (!irq_global_attr_try_claim(&global_entry->irq_attr))
                                continue;
                        if (!arch_irq_bind_delivery_on_cpu(dest_cpu, delivery_id,
                                                          slot)) {
                                irq_global_attr_abort_claim(
                                        &global_entry->irq_attr);
                                break;
                        }
                        global_entry->delivery_id = delivery_id;
                        global_entry->hwirq =
                                arch_irq_hwirq_for(delivery_id, slot);
                        global_entry->dest_cpu = dest_cpu;
                        *delivery_id_out = delivery_id;
                        return REND_SUCCESS;
                }
        }
        return -E_RENDEZVOS;
}

error_t irq_delivery_free(u32 delivery_id, cpu_id_t dest_cpu)
{
        struct global_irq *global_entry;

        if (delivery_id < ARCH_IRQ_VEC_ALLOC_LO
            || delivery_id > ARCH_IRQ_VEC_ALLOC_HI)
                return -E_IN_PARAM;
        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        global_entry = irq_global_find(delivery_id, dest_cpu);
        if (!global_entry || !irq_global_attr_is_used(&global_entry->irq_attr))
                return -E_RENDEZVOS;

        (void)arch_irq_bind_delivery_on_cpu(dest_cpu, delivery_id,
                                            IRQ_TABLE_INDEX_INVALID);
        global_entry->delivery_id = 0;
        global_entry->hwirq = IRQ_HWIRQ_INVALID;
        global_entry->dest_cpu = 0;
        global_entry->irq_handler = NULL;
        /* Clear USED last so find() stops seeing this row. */
        irq_global_attr_store(&global_entry->irq_attr, IRQ_NO_ATTR);
        return REND_SUCCESS;
}

void register_irq_handler(u32 delivery_id, cpu_id_t dest_cpu,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr)
{
        struct global_irq *global_entry;
        struct local_irq *local_entry;

        if (dest_cpu >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER) {
                pr_error("[trap] register_irq_handler: bad cpu %u\n",
                         (u32)dest_cpu);
                return;
        }

        if (irq_trap_is_local(delivery_id)) {
                local_entry = &per_cpu(local_irq_vector[delivery_id], dest_cpu);
                local_entry->irq_handler = handler;
                local_entry->irq_attr = irq_attr_set_used(irq_attr);
                return;
        }

        global_entry = irq_global_find(delivery_id, dest_cpu);
        if (!global_entry || !irq_global_attr_is_used(&global_entry->irq_attr)) {
                pr_error(
                        "[trap] register_irq_handler: delivery %u cpu %u not allocated\n",
                        delivery_id,
                        (u32)dest_cpu);
                return;
        }
        global_entry->irq_handler = handler;
        irq_global_attr_store(&global_entry->irq_attr,
                              irq_attr_set_used(irq_attr));
}

void trap_handler(struct trap_frame *tf)
{
        u64 delivery_id = TRAP_ID((tf->trap_info));
        void (*handler)(struct trap_frame *) = NULL;
        u64 attr = IRQ_NO_ATTR;

        if (irq_trap_is_local((u32)delivery_id)) {
                handler = percpu(local_irq_vector[delivery_id].irq_handler);
                attr = percpu(local_irq_vector[delivery_id].irq_attr);
        } else {
                struct global_irq *global_entry =
                        irq_global_from_delivery((u32)delivery_id);

                if (global_entry) {
                        handler = global_entry->irq_handler;
                        attr = irq_global_attr_load(&global_entry->irq_attr);
                }
        }

        if (handler) {
                handler(tf);
        } else {
                arch_unknown_trap_handler(tf);
                kernel_panic("Unhandled trap: unknown IRQ vector");
        }
        if (irq_attr_need_eoi(attr))
                arch_eoi_irq(tf->trap_info);
        if (!arch_int_from_kernel(tf) && percpu(core_tm))
                schedule(percpu(core_tm));
}

void init_interrupt(void)
{
        (void)irq_global_table_init(0);
        arch_init_interrupt();
}
