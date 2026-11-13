#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Trap: exception/IRQ entry, handler install, and line enable/disable.
 *
 * Who calls whom:
 *
 *              ┌─────────────────────────────────┐
 *              │ trap  (this module)             │
 *              └───────────────┬─────────────────┘
 *                              ▼
 *                     ┌────────────────┐
 *                     │ irq_space      │
 *                     └───────┬────────┘
 *                             ▼
 *                     ┌─────────────────────────┐
 *                     │ arch controllers        │
 *                     │ (IOAPIC / GIC SPI / …)  │
 *                     └─────────────────────────┘
 *
 * Two paths (do not conflate):
 *
 * Delivery — @c trap_handler: local id → irq_space lookup → ISR → EOI.
 * Does not program controllers; @c register_irq_handler installs the ISR.
 *
 * Line control — @c irq_enable / @c irq_disable (local coordinates):
 *   alloc + register first, then enable (space->enable programs + unmask).
 *   disable only masks; free is separate (mask before free).
 *   Migrate: disable (the irq's old owner cpu),wait → free → alloc(set irq
 * space record to new owner) → enable(the old owner enable it, not using IPI to
 * let new owner do).
 *
 * This header includes irq_space only — never arch controller headers.
 *
 * @c register_irq_handler needs a concrete @c cpu_id (@c CPU_ID_INVALID
 * forbidden). Global path / enable / disable use @c irq_global_find(local,
 * cpu); register also checks the caller is the owner.
 *
 * Ownership window (we do not using per-entry refcount to manage the life of
 * entry): from line @c irq_enable until @c irq_global_free finishes, only the
 * owner CPU operates that entry (disable/free/register/delivery). Before
 * enable, other CPUs may update soft state (migrate: old owner alloc then
 * enable for the new affinity). @c irq_global_free keeps @c irq_handler so
 * migrate need not re-register.
 */

/* === Migrate Problem !!! ===
 * This thing is a bit complex, emmm, let me tell you.
 * Do not change the following comments from the @note_start to @note_end.
 *
 * @note_start
 * 1.why the migrate must all be done by the old owner core?
 *
 * There's a race condition. Consider core A and core B, the core A owns the
 * interrupt I at the beginning.
 *
 * And then an I int happens, core A start running the handler function.
 * Then the core B try to change the mapping relationship and set the affinity
 * of I to core B. Fininally, a handler running on core A, and the I's affinity
 * is B. It's unsafe, for example, when the handler try's to irq_global_find in
 * a trap_handler function, it might cannot find the struct arch_global_irq with
 * the new mapping relationship.
 *
 * So, why all the migrate ops be done by the old owner can solve the problem?
 * The upper race condition is caused by the affinity is change by the non-owner
 * core. And if the change affinity and interrupt is all handled on one core
 * with the interrupt are enabled, it cannot do those two things at the same
 * time after the interrupt I is masked and cleaned. After we mask the interrupt
 * I(but all other ints are enabled), the new int from I will not come, and the
 * owner core now change the affinity of I is safe. It will alloc a local irq id
 * on core B, and map it and then unmask the I, but it's also safe when change
 * the mapping relationship of core B.
 *
 * So, how the upper layer using migrate? If the caller is on the owner core,
 * just disable, free, alloc another core's local irq id and map, and enable
 * it(irq_global_alloc allows lock another core's local irq id with CAS unused
 * attr). If not, the upper layer need to send a IPC to the boot thread on the
 * owner core(because all the message sent to the system should be sent to the
 * boot thread as we designed in IPC part. And we need the instruction stream
 * with the interrupt enabled, otherwise the interrupt I which happened before
 * we mask cannot be handled correctly). And the boot thread of the owner core
 * will disable, wait, free, alloc, and enable it.
 *
 * 2. why need to wait before we free, even we have masked the interrupt I.
 *
 * The order of a interrupt is like that(without MSI/MSI-x case):
 * Device → Pin → global irq distributor(like IOAPIC/GIC gicd, etc.) → core
 * local intrrupt interface(like GIC gicc/LAPIC, etc.) → core
 *
 * The mask work only stop a new interrupt I sent on Pin → global irq
 * distributor, but an old pending irq might exists on local intrrupt interface.
 * So after we mask the interrupt I, an pending I still have a chance to
 * trigger(inflight).
 *
 * The way to solve this problem is just enable interrupt and wait this(might
 * exists) interrupt I be handled. Of course we assume that the current chips
 * only pending one irq.
 * @note_end
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
 * @brief Route a claimed irq and unmask it via @c entry->space->enable.
 *
 * Expects @c irq_global_alloc and a non-NULL @c irq_handler. @p local_irq_id
 * and @p cpu_id are the affinity pair from alloc (caller need not be that
 * CPU — migrate enables from the old owner).
 *
 * @param local_irq_id Delivery id on @p cpu_id.
 * @param cpu_id       Affinity CPU from alloc (concrete).
 * @param trigger      Edge or level.
 * @param polarity     Active high or low (backend may ignore).
 *
 * @return @c REND_SUCCESS on success.
 * @retval -E_IN_PARAM     Bad ids / trigger / polarity.
 * @retval -E_REND_NOFOUND Not claimed, affinity unset, or no handler.
 * @retval -E_RENDEZVOS    Entry has no space ops.
 * @return Else an error from @c space->enable.
 */
error_t irq_enable(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                   enum irq_trigger trigger, enum irq_polarity polarity);

/**
 * @brief Mask the line then drain local pending; keep the soft claim.
 *
 * Must run on the owning CPU with IRQs enabled (e.g. owner boot thread):
 * find by @p local_irq_id on this CPU → @c space->disable → wait while
 * @c arch_irq_local_pending. Use before @c irq_global_free / migrate.
 *
 * Mask-only (nosync) is not exported yet; split later if needed.
 *
 * @param local_irq_id Delivery id on the owning (current) CPU.
 *
 * @return @c REND_SUCCESS on success.
 * @retval -E_IN_PARAM     Bad id or caller is not the recorded owner.
 * @retval -E_REND_NOFOUND Entry not claimed or affinity/local unset.
 * @return Else an error from @c space->disable.
 */
error_t irq_disable(local_irq_id_t local_irq_id);

/**
 * @brief Whether @p local_irq_id may still run a handler on *this* CPU
 *        without unmasking the line again.
 *
 * After @c space->disable, @c irq_disable spins while this is true
 * (owner CPU, IRQs enabled).
 *
 * @param local_irq_id Delivery id from the global entry (arch-defined:
 *        the same id used for dispatch on this CPU after alloc/map).
 * @return true — keep waiting; false — safe to free/teardown on this CPU.
 */
bool arch_irq_local_pending(local_irq_id_t local_irq_id);

/**
 * @brief CPU-interface EOI after a handled IRQ (before optional space eoi).
 * @param trap_info Packed trap id / source fields from @c trap_frame.
 */
void arch_local_eoi(u64 trap_info);

/**
 * @brief Per-CPU interrupt bring-up.
 *
 * Calls @c arch_init_interrupt.
 */
void init_interrupt(void);

/**
 * @brief Common trap/IRQ entry after arch decoded @c tf->trap_info.
 *
 * Looks up the ISR in irq_space, runs it, optionally @c arch_local_eoi then
 * optional @c space->eoi, then may @c schedule if from user context.
 *
 * @param tf Trap frame (must have @c trap_info filled).
 */
void trap_handler(struct trap_frame *tf);

#endif
