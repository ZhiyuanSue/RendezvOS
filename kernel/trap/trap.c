#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/barrier.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/system/panic.h>

extern Task_Manager *core_tm;

error_t irq_enable(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                   enum irq_trigger trigger, enum irq_polarity polarity)
{
        struct arch_global_irq *entry;

        if (trigger != IRQ_TRIGGER_EDGE && trigger != IRQ_TRIGGER_LEVEL)
                return -E_IN_PARAM;
        if (polarity != IRQ_POLARITY_ACTIVE_HIGH
            && polarity != IRQ_POLARITY_ACTIVE_LOW)
                return -E_IN_PARAM;
        error_t e = irq_global_find(local_irq_id, cpu_id, &entry);
        if (e != REND_SUCCESS)
                return e;
        if (!entry->irq_handler)
                return -E_REND_NOFOUND;
        if (!entry->space || !entry->space->enable)
                return -E_RENDEZVOS;
        return entry->space->enable(entry, trigger, polarity);
}

error_t irq_disable(local_irq_id_t local_irq_id)
{
        struct arch_global_irq *entry;

        error_t e = irq_global_find(local_irq_id, CPU_ID_INVALID, &entry);
        if (e != REND_SUCCESS)
                return e;
        if (!entry->space || !entry->space->disable)
                return -E_RENDEZVOS;

        e = entry->space->disable(entry);
        if (e != REND_SUCCESS)
                return e;
        /** Do not change the following comments:
         * even the arch target irq is disabled,
         * There still might have a pending irq.
         * just see the comments at the begining of the trap.h
         * here is the wait part.
         * But here only just check the arch pending register is enough.
         * for some arch, such a check also no need.
         */
        while (arch_irq_local_pending(entry->local_irq_id))
                arch_cpu_relax();
        return REND_SUCCESS;
}

void register_irq_handler(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr)
{
        if (irq_trap_is_local(local_irq_id)) {
                struct local_irq *local_entry =
                        irq_local_find(local_irq_id, cpu_id);
                if (!local_entry) {
                        pr_error(
                                "[trap] register_irq_handler: local_irq_id %zu cpu %u find failed\n",
                                local_irq_id,
                                (u32)cpu_id);
                        return;
                }
                local_entry->irq_handler = handler;
                local_entry->irq_attr = irq_attr_set_used(irq_attr);
        } else {
                struct arch_global_irq *global_entry;
                error_t e;

                if (cpu_id == CPU_ID_INVALID
                    || percpu(cpu_number) != cpu_id) {
                        pr_error(
                                "[trap] register_irq_handler: local_irq_id %zu cpu %u not owner (caller cpu %u)\n",
                                local_irq_id,
                                (u32)cpu_id,
                                (u32)percpu(cpu_number));
                        return;
                }
                e = irq_global_find(local_irq_id, cpu_id, &global_entry);
                if (e != REND_SUCCESS) {
                        pr_error(
                                "[trap] register_irq_handler: local_irq_id %zu cpu %u find failed (%d)\n",
                                local_irq_id,
                                (u32)cpu_id,
                                (int)e);
                        return;
                }
                global_entry->irq_handler = handler;
                irq_atomic_attr_store(&global_entry->irq_attr,
                                      irq_attr_set_used(irq_attr));
        }
}

void trap_handler(struct trap_frame *tf)
{
        local_irq_id_t local_irq_id = (local_irq_id_t)TRAP_ID((tf->trap_info));
        void (*handler)(struct trap_frame *) = NULL;
        u64 attr = IRQ_NO_ATTR;
        struct arch_global_irq *global_entry = NULL;

        if (irq_trap_is_local(local_irq_id)) {
                struct local_irq *local_entry =
                        irq_local_find(local_irq_id, CPU_ID_INVALID);

                if (local_entry) {
                        handler = local_entry->irq_handler;
                        attr = local_entry->irq_attr;
                }
        } else {
                if (irq_global_find(local_irq_id, CPU_ID_INVALID, &global_entry)
                    == REND_SUCCESS) {
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
        if (irq_attr_need_eoi(attr)) {
                arch_local_eoi(tf->trap_info);
                if (global_entry && global_entry->space
                    && global_entry->space->eoi)
                        global_entry->space->eoi(global_entry, tf->trap_info);
        }
        if (!arch_int_from_kernel(tf) && percpu(core_tm))
                schedule(percpu(core_tm));
}

void init_interrupt(void)
{
        arch_init_interrupt();
}
