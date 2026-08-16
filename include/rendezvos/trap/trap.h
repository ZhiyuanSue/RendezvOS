#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Include architecture-specific trap headers.
 * This provides:
 * - struct trap_frame (architecture-specific)
 * - Architecture-specific trap macros
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

/* ===== Existing IRQ handler interface ===== */

struct irq {
        void (*irq_handler)(struct trap_frame *tf);
#define IRQ_NO_ATTR  (0ull)
#define IRQ_NEED_EOI BIT_U64(0)
#define IRQ_VEC_USED BIT_U64(1)
        u64 irq_attr;
};

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

/**
 * @brief Install an IRQ/trap handler on every CPU's irq_vector[irq_num].
 *
 * Requires the trap id already marked USED (via reserve or irq_vector_alloc).
 * Caller @p irq_attr supplies flags such as IRQ_NEED_EOI; USED is preserved.
 *
 * @param irq_num Trap id / vector index in [0, NR_IRQ)
 * @param handler Handler invoked from trap_handler; may be NULL to clear
 * @param irq_attr Attribute flags (e.g. IRQ_NO_ATTR, IRQ_NEED_EOI)
 */
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr);

/**
 * @brief Per-CPU interrupt bring-up for arch_start_core.
 *
 * Calls arch_init_irq_vector_state() then arch_init_interrupt(). Does not
 * zero irq_vector[] (may already hold all-CPU alloc/register writes).
 */
void init_interrupt(void);

/**
 * @brief Architecture EOI / interrupt completion for the current IRQ.
 *
 * Called from trap_handler when the vector's irq_attr has IRQ_NEED_EOI.
 *
 * @param trap_info Arch trap_info from the trap frame (vector / source encoding)
 */
void arch_eoi_irq(u64 trap_info);

/**
 * @brief Mark trap ids [lo, hi] as USED on one CPU's irq_vector[].
 *
 * Used by arch_start_core / arch_init_irq_vector_state for this CPU's
 * core-owned vectors (exceptions, timer, IPI, …).
 *
 * @param cpu CPU index in [0, RENDEZVOS_MAX_CPU_NUMBER)
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id
 * @return REND_SUCCESS, or -E_IN_PARAM if cpu / range invalid
 */
error_t irq_vector_reserve_range_for_cpu(cpu_id_t cpu, u32 lo, u32 hi);

/**
 * @brief Mark trap ids [lo, hi] as USED on every CPU's irq_vector[].
 *
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id
 * @return REND_SUCCESS, or -E_IN_PARAM if range invalid
 */
error_t irq_vector_reserve_range_for_all_cpus(u32 lo, u32 hi);

/**
 * @brief Publish the inclusive trap-id window scanned by irq_vector_alloc.
 *
 * @param lo Inclusive low trap id
 * @param hi Inclusive high trap id (ignored if range invalid)
 */
void irq_vector_set_alloc_pool(u32 lo, u32 hi);

/**
 * @brief Allocate one free trap id from the alloc pool on all CPUs.
 *
 * Marks the id USED on every CPU. Caller must then register_irq_handler.
 *
 * @param trap_id_out Out: allocated trap id
 * @return REND_SUCCESS, -E_IN_PARAM, or -E_RENDEZVOS if pool exhausted
 */
error_t irq_vector_alloc(u32 *trap_id_out);

/**
 * @brief Release a pool trap id (clear USED on all CPUs).
 *
 * @param trap_id Trap id previously returned by irq_vector_alloc
 * @return REND_SUCCESS, -E_IN_PARAM, or -E_RENDEZVOS if not fully USED
 */
error_t irq_vector_free(u32 trap_id);

/**
 * @brief Arch: reserve core vectors on this CPU and set the alloc pool.
 *
 * Called from init_interrupt() on each arch_start_core path.
 */
void arch_init_irq_vector_state(void);

/* ===== Fixed trap handler interface (architecture-independent) ===== */

/**
 * @brief Fixed trap handler function type
 *
 * @param tf: trap frame
 *
 * @note Architecture code fills a per-arch `*_trap_info` via
 *       `arch_populate_trap_info(tf, &info)` before dispatching fixed handlers.
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
