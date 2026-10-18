#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Three numbers:
 * - delivery_id: what the CPU reports (x86 IDT vector / aarch64 trap_id)
 * - table_index: subscript into @c global_irq_table
 * - hwirq: hardware source id in @c global_irq::hwirq (GSI / INTID)
 *
 * Affinity: one global irq = one (delivery_id, dest_cpu). Rebind = free+alloc.
 */
#include <common/bit.h>
#include <common/stdbool.h>
#include <common/atomic.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_id.h>

#ifdef _AARCH64_
#include <arch/aarch64/trap/trap.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/trap/trap.h>
#else
#include <arch/x86_64/trap/trap.h>
#endif

struct local_irq {
        IRQ_COMMON
        u64 irq_attr; /* plain store; per-CPU, no CAS */
};

extern struct global_irq *global_irq_table;
extern u32 nr_global_irq;

/* ----- irq_attr bit helpers (work on a u64 snapshot) ----- */

static inline bool irq_attr_used(u64 attr)
{
        return (attr & IRQ_VEC_USED) != 0;
}

static inline u64 irq_attr_set_used(u64 attr)
{
        return set_mask_u64(attr, IRQ_VEC_USED);
}

static inline u64 irq_attr_set_unused(u64 attr)
{
        return clear_mask_u64(attr, IRQ_VEC_USED);
}

static inline bool irq_attr_need_eoi(u64 attr)
{
        return (attr & IRQ_NEED_EOI) != 0;
}

/* ----- global_irq::irq_attr (atomic64_t) only ----- */

static inline volatile u64 *irq_global_attr_cell(atomic64_t *attr)
{
        return (volatile u64 *)&attr->counter;
}

static inline u64 irq_global_attr_load(atomic64_t *attr)
{
        return atomic64_load(irq_global_attr_cell(attr));
}

static inline void irq_global_attr_store(atomic64_t *attr, u64 value)
{
        atomic64_store(irq_global_attr_cell(attr), value);
}

static inline bool irq_global_attr_is_used(atomic64_t *attr)
{
        return irq_attr_used(irq_global_attr_load(attr));
}

/** CAS 0 → USED on a global row; on failure try another slot (no spin). */
static inline bool irq_global_attr_try_claim(atomic64_t *attr)
{
        return atomic64_cas(irq_global_attr_cell(attr), IRQ_NO_ATTR, IRQ_VEC_USED)
               == IRQ_NO_ATTR;
}

static inline void irq_global_attr_abort_claim(atomic64_t *attr)
{
        irq_global_attr_store(attr, IRQ_NO_ATTR);
}

typedef void (*fixed_trap_handler_t)(struct trap_frame *tf);

bool irq_trap_is_local(u32 delivery_id);

struct global_irq *irq_global_from_delivery(u32 delivery_id);

struct global_irq *irq_global_find(u32 delivery_id, cpu_id_t dest_cpu);

/*
 * Arch delivery map
 *
 * @c arch_irq_fixed_table_index: aarch64 returns identity index; x86 returns
 * @c IRQ_TABLE_INDEX_INVALID meaning "any free slot".
 * @c arch_irq_bind_delivery_on_cpu: x86 CAS-binds map; returns false if the
 * delivery id is already taken on that CPU. aarch64 no-op success.
 */

u32 arch_irq_delivery_to_table_index_on_cpu(cpu_id_t cpu, u32 delivery_id);
u32 arch_irq_delivery_to_table_index(u32 delivery_id);

/** True if @p delivery_id is free to bind on @p dest_cpu. */
bool arch_irq_delivery_available(cpu_id_t dest_cpu, u32 delivery_id);

/** Fixed table index for @p delivery_id, or @c IRQ_TABLE_INDEX_INVALID to scan. */
u32 arch_irq_fixed_table_index(u32 delivery_id);

/** Hardware hwirq to store after a successful claim. */
u32 arch_irq_hwirq_for(u32 delivery_id, u32 table_index);

/**
 * Bind or clear per-CPU delivery→table_index.
 * @p table_index @c IRQ_TABLE_INDEX_INVALID clears.
 * @return false if bind lost a race (delivery already mapped on that CPU).
 */
bool arch_irq_bind_delivery_on_cpu(cpu_id_t cpu, u32 delivery_id,
                                   u32 table_index);

void arch_eoi_irq(u64 trap_info);

void arch_register_fixed_trap(enum trap_class trap_class,
                              fixed_trap_handler_t handler, u64 irq_attr);

void register_irq_handler(u32 delivery_id, cpu_id_t dest_cpu,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);

void init_interrupt(void);

error_t irq_global_table_init(u32 count);

error_t irq_delivery_alloc(u32 *delivery_id_out, cpu_id_t dest_cpu);

error_t irq_delivery_free(u32 delivery_id, cpu_id_t dest_cpu);

#endif
