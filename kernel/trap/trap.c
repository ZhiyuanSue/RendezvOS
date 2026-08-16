#include <modules/log/log.h>
#include <common/stddef.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/common.h>
#include <rendezvos/system/panic.h>
extern Task_Manager *core_tm;
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);

static u32 irq_vector_pool_lo = 1;
static u32 irq_vector_pool_hi = 0;

static bool irq_vector_cpu_legal(cpu_id_t cpu)
{
        return cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER;
}

static bool irq_vector_range_legal(u32 lo, u32 hi)
{
        return lo <= hi && hi < (u32)NR_IRQ;
}

error_t irq_vector_reserve_range_for_cpu(cpu_id_t cpu, u32 lo, u32 hi)
{
        u32 id;

        if (!irq_vector_cpu_legal(cpu) || !irq_vector_range_legal(lo, hi))
                return -E_IN_PARAM;

        for (id = lo; id <= hi; id++) {
                u64 *attr = &per_cpu(irq_vector[id].irq_attr, cpu);
                *attr = irq_attr_set_used(*attr);
        }
        return REND_SUCCESS;
}

error_t irq_vector_reserve_range_for_all_cpus(u32 lo, u32 hi)
{
        cpu_id_t cpu;
        error_t e;

        for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                e = irq_vector_reserve_range_for_cpu(cpu, lo, hi);
                if (e != REND_SUCCESS)
                        return e;
        }
        return REND_SUCCESS;
}

void irq_vector_set_alloc_pool(u32 lo, u32 hi)
{
        if (!irq_vector_range_legal(lo, hi))
                return;
        irq_vector_pool_lo = lo;
        irq_vector_pool_hi = hi;
}

error_t irq_vector_alloc(u32 *trap_id_out)
{
        u32 id;
        cpu_id_t cpu;
        bool free;

        if (!trap_id_out
            || !irq_vector_range_legal(irq_vector_pool_lo, irq_vector_pool_hi))
                return -E_IN_PARAM;

        for (id = irq_vector_pool_lo; id <= irq_vector_pool_hi; id++) {
                free = true;
                for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                        if (irq_attr_used(
                                    per_cpu(irq_vector[id].irq_attr, cpu))) {
                                free = false;
                                break;
                        }
                }
                if (!free)
                        continue;

                for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                        u64 *attr = &per_cpu(irq_vector[id].irq_attr, cpu);
                        *attr = irq_attr_set_used(*attr);
                }
                *trap_id_out = id;
                return REND_SUCCESS;
        }
        return -E_RENDEZVOS;
}

error_t irq_vector_free(u32 trap_id)
{
        cpu_id_t cpu;

        if (trap_id < irq_vector_pool_lo || trap_id > irq_vector_pool_hi
            || trap_id >= (u32)NR_IRQ)
                return -E_IN_PARAM;

        for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                if (!irq_attr_used(per_cpu(irq_vector[trap_id].irq_attr, cpu)))
                        return -E_RENDEZVOS;
        }
        for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                u64 *attr = &per_cpu(irq_vector[trap_id].irq_attr, cpu);

                *attr = irq_attr_set_unused(*attr);
        }
        return REND_SUCCESS;
}

void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr)
{
        cpu_id_t cpu;

        if (irq_num < 0 || (u32)irq_num >= (u32)NR_IRQ) {
                pr_error("[trap] register_irq_handler: bad id %d\n", irq_num);
                return;
        }
        if (!irq_attr_used(percpu(irq_vector[irq_num].irq_attr))) {
                pr_error("[trap] register_irq_handler: trap_id %d not in use\n",
                         irq_num);
                return;
        }
        for (cpu = 0; cpu < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; cpu++) {
                struct irq *entry = &per_cpu(irq_vector[irq_num], cpu);

                entry->irq_handler = handler;
                entry->irq_attr = irq_attr_set_used(irq_attr);
        }
}

void trap_handler(struct trap_frame *tf)
{
        u64 trap_id = TRAP_ID((tf->trap_info));
        if (percpu(irq_vector[trap_id].irq_handler)) {
                percpu(irq_vector[trap_id].irq_handler)(tf);
        } else {
                arch_unknown_trap_handler(tf);
                kernel_panic("Unhandled trap: unknown IRQ vector");
        }
        if (irq_attr_need_eoi(percpu(irq_vector[trap_id].irq_attr))) {
                arch_eoi_irq(tf->trap_info);
        }
        if (!arch_int_from_kernel(tf) && percpu(core_tm))
                schedule(percpu(core_tm));
}
void init_interrupt(void)
{
        arch_init_irq_vector_state();
        arch_init_interrupt();
}