#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Trap: exception/IRQ entry, handler install, and global line enable/disable.
 *
 * Who calls whom:
 *
 *              ┌─────────────────────────────────┐
 *              │ trap  (this module)             │
 *              └───────────────┬─────────────────┘
 *                 ┌────────────┴────────────┐
 *                 ▼                         ▼
 *        ┌────────────────┐       ┌─────────────────────────┐
 *        │ irq_space      │◄──────│ arch irqchip            │
 *        └────────────────┘       └─────────────────────────┘
 *
 * Two paths (do not conflate):
 *
 * Delivery — @c trap_handler: vector → irq_space lookup → ISR → EOI.
 * Does not program the irqchip; @c register_irq_handler installs the ISR.
 *
 * Line control — @c irq_enable / @c irq_disable:
 *   alloc + register first, then enable (arch programs the chip + unmask).
 *   disable only masks; free is separate (mask before free).
 *   Migrate: disable → free → alloc → enable.
 *
 * This header includes irq_space only — never arch irqchip headers.
 *
 * @c register_irq_handler needs a concrete @c cpu_id (@c CPU_ID_INVALID
 * forbidden). Global path: caller must be the owning CPU (irq_space rules).
 */
#include <rendezvos/trap/irq_space.h>

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
 * @brief Fixed-trap ISR type (page fault, breakpoint, …).
 * @param tf Trap frame at entry.
 */
typedef void (*fixed_trap_handler_t)(struct trap_frame *tf);

/**
 * @brief Arch EOI after a handled IRQ.
 * @param trap_info Packed trap id / source fields from @c trap_frame.
 */
void arch_eoi_irq(u64 trap_info);

/**
 * @brief Register a fixed-trap handler for @p trap_class on every CPU.
 * @param trap_class Semantic class from @c enum trap_class.
 * @param handler    ISR to install (via @c register_irq_handler).
 * @param irq_attr   Soft attr bits (e.g. @c IRQ_NEED_EOI); OR'd with USED.
 */
void arch_register_fixed_trap(enum trap_class trap_class,
                              fixed_trap_handler_t handler, u64 irq_attr);

/**
 * @brief Install or clear an ISR for @p local_irq_id on @p cpu_id.
 *
 * - **Local** (@p local_irq_id &lt; @c NR_ARCH_LOCAL_IRQ): writes that CPU's
 *   @c local_irq_vector slot. Caller may target another CPU (fixed-trap
 *   bring-up).
 * - **Global**: requires a prior @c irq_global_alloc for
 *   (@p local_irq_id, @p cpu_id); caller must be the owning CPU.
 *
 * @param local_irq_id Per-CPU id (or virtual id mapped to a global entry).
 * @param cpu_id       Concrete CPU; @c CPU_ID_INVALID is forbidden.
 * @param handler      ISR, or @c NULL to clear (e.g. before unload).
 * @param irq_attr     Soft bits such as @c IRQ_NEED_EOI; USED is set.
 *
 * @note Migrate without changing the ISR need not register again — free
 *       keeps @c irq_handler.
 */
void register_irq_handler(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);


/**
 * @brief Route a claimed global irq on the irqchip and unmask it.
 *
 * Expects @c irq_global_alloc and a non-NULL @c irq_handler on
 * @c global_irq_vector[@p global_irq_id]. Loads @c local_irq_id / @c cpu_id
 * from that entry and calls @c arch_irq_enable.
 *
 * @param global_irq_id Index into @c global_irq_vector.
 * @param trigger       Edge or level.
 * @param polarity      Active high or low (arch may ignore).
 *
 * @return @c REND_SUCCESS on success.
 * @retval -E_IN_PARAM     Bad id / trigger / polarity.
 * @retval -E_REND_NOFOUND Not claimed, affinity unset, or no handler.
 * @return Else an error from @c arch_irq_enable.
 */
error_t irq_enable(global_irq_id_t global_irq_id, enum irq_trigger trigger,
                   enum irq_polarity polarity);

/**
 * @brief Mask hardware delivery for @p global_irq_id; keep the soft claim.
 *
 * Does not clear @c irq_handler or drop @c IRQ_ATTR_USED. Call before
 * @c irq_global_free when tearing down or migrating.
 *
 * @param global_irq_id Index into @c global_irq_vector.
 *
 * @return @c REND_SUCCESS on success.
 * @retval -E_IN_PARAM     Bad id.
 * @retval -E_REND_NOFOUND Entry not marked USED.
 * @return Else an error from @c arch_irq_disable.
 */
error_t irq_disable(global_irq_id_t global_irq_id);

/**
 * @brief Arch irqchip hook for @c irq_enable.
 *
 * Declared here so this header never includes irqchip headers. Drivers
 * should call @c irq_enable, not this hook directly.
 *
 * @param global_irq_id Same as @c irq_enable.
 * @param local_irq_id  Delivery id from the global entry (arch-defined).
 * @param cpu_id        Destination CPU recorded at alloc.
 * @param trigger       Edge or level.
 * @param polarity      Active high or low (arch may ignore).
 *
 * @return @c REND_SUCCESS or an arch error.
 */
error_t arch_irq_enable(global_irq_id_t global_irq_id,
                        local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                        enum irq_trigger trigger, enum irq_polarity polarity);

/**
 * @brief Arch irqchip hook for @c irq_disable (mask only).
 *
 * @param global_irq_id Same as @c irq_disable.
 * @return @c REND_SUCCESS or an arch error.
 */
error_t arch_irq_disable(global_irq_id_t global_irq_id);

/**
 * @brief Per-CPU interrupt bring-up.
 *
 * Calls @c arch_init_interrupt.
 */
void init_interrupt(void);

/**
 * @brief Common trap/IRQ entry after arch decoded @c tf->trap_info.
 *
 * Looks up the ISR in irq_space, runs it, optionally @c arch_eoi_irq, then
 * may @c schedule if the trap came from user context.
 *
 * @param tf Trap frame (must have @c trap_info filled).
 */
void trap_handler(struct trap_frame *tf);

#endif
