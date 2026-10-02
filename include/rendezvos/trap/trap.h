#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Architecture trap headers provide @c struct trap_frame and arch macros
 * (including @c trap_class).
 */
#include <common/bit.h>
#include <common/stdbool.h>
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

/**
 * @brief Per-CPU IRQ / trap vector slot: handler + attribute flags.
 */
struct irq {
        void (*irq_handler)(struct trap_frame *tf);
#define IRQ_NO_ATTR  (0ull) /* No attribute bits. */
#define IRQ_NEED_EOI BIT_U64(0) /* @c trap_handler should send EOIs after return.*/
#define IRQ_VEC_USED BIT_U64(1) /* Slot reserved / allocated. */
        u64 irq_attr;
};

/** True if @p attr has @c IRQ_VEC_USED . */
static inline bool irq_attr_used(u64 attr)
{
        return (attr & IRQ_VEC_USED) != 0;
}

/** @p attr set @c IRQ_VEC_USED . */
static inline u64 irq_attr_set_used(u64 attr)
{
        return set_mask_u64(attr, IRQ_VEC_USED);
}

/** @p attr clear @c IRQ_VEC_USED . */
static inline u64 irq_attr_set_unused(u64 attr)
{
        return clear_mask_u64(attr, IRQ_VEC_USED);
}

/** True if @p attr has @c IRQ_NEED_EOI . */
static inline bool irq_attr_need_eoi(u64 attr)
{
        return (attr & IRQ_NEED_EOI) != 0;
}

/**
 * @brief Install an IRQ/trap handler on every CPU's irq_vector[irq_num].
 *
 * Requires the trap id already marked USED on the **calling CPU** (via
 * reserve or @c irq_vector_alloc). Writes the same @p handler / attr to
 * all CPUs' slots (v0.1 has no per-CPU different ISRs). Caller
 * @p irq_attr supplies flags such as @c IRQ_NEED_EOI; USED is forced on.
 *
 * Call order for devices: @c irq_vector_alloc → @c register_irq_handler →
 * then platform unmask / route. Do not EOI inside the
 * handler when @c IRQ_NEED_EOI is set — @c trap_handler does it after return.
 *
 * @param irq_num Trap id / vector index in [0, @c NR_IRQ)
 * @param handler Handler invoked from @c trap_handler; may be NULL to clear
 * @param irq_attr Attribute flags 
 */
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr);

/**
 * @brief Per-CPU interrupt bring-up for @c arch_start_core.
 *
 * Reserves this CPU's fixed vectors / publishes the alloc pool, then loads
 * the arch interrupt table. Does not zero @c irq_vector[] (earlier CPUs may
 * already have written all-CPU alloc/register state).
 */
void init_interrupt(void);

/**
 * @brief Architecture EOI / interrupt completion for the current IRQ.
 *
 * Called from @c trap_handler when the vector's @c irq_attr has @c IRQ_NEED_EOI.
 *
 * @param trap_info Arch trap_info from the trap frame (vector / source encoding)
 */
void arch_eoi_irq(u64 trap_info);

/**
 * @brief Mark trap ids [lo, hi] as USED on one CPU's irq_vector[].
 *
 * Used by @c arch_init_irq_vector_state for this CPU's core-owned vectors.
 * Does not install a handler.
 *
 * @param cpu CPU index in [0, @c RENDEZVOS_MAX_CPU_NUMBER)
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id
 * @return @c REND_SUCCESS, or @c -E_IN_PARAM if cpu / range invalid
 */
error_t irq_vector_reserve_range_for_cpu(cpu_id_t cpu, u32 lo, u32 hi);

/**
 * @brief Mark trap ids [lo, hi] as USED on every CPU's irq_vector[].
 *
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id
 * @return @c REND_SUCCESS, or @c -E_IN_PARAM if range invalid
 */
error_t irq_vector_reserve_range_for_all_cpus(u32 lo, u32 hi);

/**
 * @brief Publish the inclusive trap-id window scanned by @c irq_vector_alloc.
 *
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id
 */
void irq_vector_set_alloc_pool(u32 lo, u32 hi);

/**
 * @brief Allocate one free trap id from the alloc pool on all CPUs.
 *
 * Scans [pool_lo, pool_hi]; an id is free only if no CPU has @c USED set.
 * On success marks @c USED on every CPU. Caller must then
 * @c register_irq_handler before enabling the hardware source.
 *
 * @param trap_id_out Out: allocated trap id
 * @return @c REND_SUCCESS, @c -E_IN_PARAM (null / pool unset), or
 *         @c -E_RENDEZVOS if pool exhausted
 */
error_t irq_vector_alloc(u32 *trap_id_out);

/**
 * @brief Release a pool trap id (clear USED on all CPUs).
 *
 * Only ids inside the current alloc pool. Requires @c USED on every CPU;
 * otherwise @c -E_RENDEZVOS. Does not clear @c irq_handler — re-alloc of
 * the same id may see a stale pointer until @c register_irq_handler again.
 *
 * @param trap_id Trap id previously returned by @c irq_vector_alloc
 * @return @c REND_SUCCESS, @c -E_IN_PARAM, or @c -E_RENDEZVOS if not fully USED
 */
error_t irq_vector_free(u32 trap_id);

/**
 * @brief Arch: reserve this CPU's fixed slots and publish the device alloc pool.
 *
 * Called once per CPU from @c init_interrupt. Reserves this CPU's core-owned
 * fixed vectors and publishes the global device alloc window via
 * @c irq_vector_set_alloc_pool (idempotent across CPUs).
 */
void arch_init_irq_vector_state(void);

/* ===== Fixed trap handler interface (architecture-independent) ===== */

/**
 * @brief Fixed / semantic trap handler: receives the raw @c trap_frame.
 * @param tf: trap frame
 *
 * @note Arch code fills a per-arch @c *_trap_info via
 * @c arch_populate_trap_info(tf, &info) before dispatching fixed handlers.
 */
typedef void (*fixed_trap_handler_t)(struct trap_frame *tf);

/**
 * @brief Register fixed trap handler (architecture-independent interface)
 *
 * @param trap_class: trap type (enum trap_class from trap_common.h)
 * @param handler: handler function
 * @param irq_attr: IRQ attributes (e.g., IRQ_NEED_EOI)
 *
 * @note Architecture layer maps trap_class to specific trap ID(s).
 *       For aarch64, one trap_class may map to multiple EC values.
 *       Repeated registration overwrites previous handler.
 */
void register_fixed_trap(enum trap_class trap_class,
                         fixed_trap_handler_t handler, u64 irq_attr);

#endif
