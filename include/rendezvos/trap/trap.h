#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Two ids, matching the two arrays:
 * - local_irq_id: per-CPU id. Below @c NR_ARCH_LOCAL_IRQ it is the subscript
 *   of @c local_irq_vector. At or above that limit it cannot index the local
 *   array, so it is mapped to a global_irq_id.
 * - global_irq_id: subscript of @c global_irq_vector. x86: GSI. aarch64:
 *   SPI index (INTID - SPI_START).
 *
 * x86 map is per-CPU: local_irq_id → global_irq_id. aarch64 SPI is a formula.
 * Affinity: one global irq = one (local_irq_id, dest_cpu). Remap = free+alloc.
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

/*
 * Allocated once in @c arch_start_platform via @c irq_global_table_alloc.
 * @c nr_global_irq is the length of that table (GSI / SPI count).
 */
extern struct global_irq *global_irq_vector;
extern size_t nr_global_irq;

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

bool irq_trap_is_local(size_t local_irq_id);

struct global_irq *irq_global_from_local_id(size_t local_irq_id);

struct global_irq *irq_global_find(size_t local_irq_id, cpu_id_t dest_cpu);

/*
 * local_irq_id → global_irq_id map.
 *
 * @c arch_map_fixed_local_irq_id: aarch64 returns the trap id for this
 * global_irq_id. x86 returns @c IRQ_LOCAL_ID_INVALID: alloc picks a free
 * local_irq_id on dest_cpu and maps it.
 * @c arch_map_local_irq_id: x86 CAS-updates the per-CPU map. aarch64 no-op.
 */

size_t arch_map_global_irq_id_on_cpu(cpu_id_t cpu, size_t local_irq_id);
size_t arch_map_global_irq_id(size_t local_irq_id);

/** True if @p local_irq_id is not mapped on @p dest_cpu. */
bool arch_local_irq_id_available(cpu_id_t dest_cpu, size_t local_irq_id);

/**
 * local_irq_id fixed by @p global_irq_id, or @c IRQ_LOCAL_ID_INVALID when
 * alloc must pick a free local_irq_id on the destination CPU (x86).
 */
size_t arch_map_fixed_local_irq_id(size_t global_irq_id);

/**
 * Map or clear per-CPU local_irq_id → global_irq_id.
 * @p global_irq_id @c IRQ_GLOBAL_ID_INVALID clears the map entry.
 * @return false if this local_irq_id is already mapped on @p cpu.
 */
bool arch_map_local_irq_id(cpu_id_t cpu, size_t local_irq_id,
                           size_t global_irq_id);

void arch_eoi_irq(u64 trap_info);

void arch_register_fixed_trap(enum trap_class trap_class,
                              fixed_trap_handler_t handler, u64 irq_attr);

void register_irq_handler(size_t local_irq_id, cpu_id_t dest_cpu,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);

void init_interrupt(void);

/**
 * @brief Allocate @c global_irq_vector of @p count rows and set @c nr_global_irq.
 *
 * Called once from @c arch_start_platform on the BSP, after @c kallocator
 * exists. @p count is the platform external-irq count (x86 GSI, aarch64 SPI).
 * Slots are zeroed; @c irq_attr starts unused.
 *
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if @p count is 0,
 *         @c -E_RENDEZVOS if already allocated or @c kallocator fails.
 */
error_t irq_global_table_alloc(size_t count);

/**
 * Claim @p global_irq_id and map a local_irq_id on @p dest_cpu.
 * x86 picks a free local_irq_id. aarch64 uses the fixed SPI trap id.
 * @p local_irq_id_out receives the id to pass to @c register_irq_handler.
 */
error_t irq_global_alloc(size_t global_irq_id, cpu_id_t dest_cpu,
                         size_t *local_irq_id_out);

error_t irq_global_free(size_t global_irq_id, cpu_id_t dest_cpu);

#endif
