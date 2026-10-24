#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/system/panic.h>

extern Task_Manager *core_tm;

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
                /*
                 * Global path: after irq_global_alloc, only the owning CPU
                 * may install or replace the handler (same rule as free).
                 */
                if (percpu(cpu_number) != cpu_id) {
                        pr_error(
                                "[trap] register_irq_handler: global local_irq_id %zu cpu %u not owner (caller cpu %u)\n",
                                local_irq_id,
                                (u32)cpu_id,
                                (u32)percpu(cpu_number));
                        return;
                }

                struct arch_global_irq *global_entry =
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
                struct arch_global_irq *global_entry =
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
