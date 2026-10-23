#ifndef _RENDEZVOS_IRQ_SPACE_H_
#define _RENDEZVOS_IRQ_SPACE_H_

/*
 * irq_space — software id tables only.
 *
 * This module owns local_irq_vector / global_irq_vector, the local↔global
 * map, and @c irq_global_* claim/release. It does not program interrupt
 * controllers (route, mask, EOI registers): that is platform code.
 * Dispatch (@c trap_handler / @c register_irq_handler) lives in
 * @c rendezvos/trap/trap.h; register's global path follows the ownership
 * rules below.
 *
 * Two ids:
 * - local_irq_id: per-CPU number line. Below @c NR_ARCH_LOCAL_IRQ it indexes
 *   @c local_irq_vector; at or above that limit it maps to a global_irq_id.
 * - global_irq_id: index into @c global_irq_vector (GSI / SPI index, etc.).
 *
 * Ownership (global entries):
 * - Idle claim may race; @c IRQ_ATTR_USED CAS picks a winner, who records
 *   (local_irq_id, cpu_id). Any CPU may alloc for a target cpu_id.
 * - After claim, only the owning CPU may free or (global) register.
 *   @c IRQ_ATTR_USED is not a refcount; find returns a raw vector slot.
 * - Free clears the claim with @c irq_atomic_attr_try_set_unused only:
 *   keeps @c irq_handler and sticky bits such as @c IRQ_NEED_EOI so a later
 *   alloc on another CPU can reuse the same ISR. Clear the handler with
 *   @c register_irq_handler(..., NULL, ...) on the owner before unload.
 * - Migrate (caller also stops delivery in hardware): free on owner →
 *   alloc for the new CPU → reprogram route. No register unless the ISR
 *   changes.
 *
 * @c CPU_ID_INVALID: map/find → current CPU. alloc/free/global register
 * need a concrete cpu_id; free and global register also require
 * caller == that cpu_id.
 */
#include <common/bit.h>
#include <common/stdbool.h>
#include <common/atomic.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/trap_common.h>

#ifdef _AARCH64_
#include <arch/aarch64/trap/irq_space.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/trap/irq_space.h>
#else
#include <arch/x86_64/trap/irq_space.h>
#endif

struct local_irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr; /* plain store; per-CPU, no CAS */
};

/*
 * Allocated once in @c arch_start_platform via @c irq_global_vector_alloc.
 * @c nr_global_irq is the length of that vector.
 */
extern struct arch_global_irq *global_irq_vector;
extern size_t nr_global_irq;

/* Per-CPU; @c DEFINE_PER_CPU in irq_space.c. */
extern struct local_irq local_irq_vector[NR_ARCH_LOCAL_IRQ];

/* ----- irq_attr bit helpers (work on a u64 snapshot) ----- */

static inline bool irq_attr_used(u64 attr)
{
        return (attr & IRQ_ATTR_USED) != 0;
}

static inline u64 irq_attr_set_used(u64 attr)
{
        return set_mask_u64(attr, IRQ_ATTR_USED);
}

static inline u64 irq_attr_set_unused(u64 attr)
{
        return clear_mask_u64(attr, IRQ_ATTR_USED);
}

static inline bool irq_attr_need_eoi(u64 attr)
{
        return (attr & IRQ_NEED_EOI) != 0;
}

/*
 * Atomic irq_attr helpers (atomic64_t storage).
 * Plain u64 attrs use irq_attr_* on a snapshot, then a normal store.
 */

static inline u64 irq_atomic_attr_load(atomic64_t *attr)
{
        return atomic64_load((volatile const u64 *)&attr->counter);
}

static inline void irq_atomic_attr_store(atomic64_t *attr, u64 value)
{
        atomic64_store((volatile u64 *)&attr->counter, value);
}

static inline bool irq_atomic_attr_is_used(atomic64_t *attr)
{
        return irq_attr_used(irq_atomic_attr_load(attr));
}

/**
 * Apply @c irq_attr_set_used to the current value and CAS it in.
 * False if already used, or if a concurrent write won the CAS.
 */
static inline bool irq_atomic_attr_try_set_used(atomic64_t *attr)
{
        u64 old_attr;
        u64 new_attr;

        old_attr = irq_atomic_attr_load(attr);
        if (irq_attr_used(old_attr))
                return false;
        new_attr = irq_attr_set_used(old_attr);
        return atomic64_cas((volatile u64 *)&attr->counter, old_attr, new_attr)
               == old_attr;
}

/**
 * Apply @c irq_attr_set_unused to the current value and CAS it in.
 * False if already unused, or if a concurrent write won the CAS.
 */
static inline bool irq_atomic_attr_try_set_unused(atomic64_t *attr)
{
        u64 old_attr;
        u64 new_attr;

        old_attr = irq_atomic_attr_load(attr);
        if (!irq_attr_used(old_attr))
                return false;
        new_attr = irq_attr_set_unused(old_attr);
        return atomic64_cas((volatile u64 *)&attr->counter, old_attr, new_attr)
               == old_attr;
}

bool irq_trap_is_local(size_t local_irq_id);

/**
 * Look up the global irq entry for @p local_irq_id on @p cpu_id.
 * @p cpu_id @c CPU_ID_INVALID means the current CPU.
 * @return the entry, or NULL if not allocated / affinity mismatch.
 */
struct arch_global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id);

/* === arch map helpers (arch implements in arch/<arch>/trap/irq_space.c) === */
/*
 * local_irq_id ↔ global_irq_id mapping.
 *
 * For APIs that take @p cpu_id: pass a concrete id, or @c CPU_ID_INVALID
 * to mean the current CPU.
 */

/** Resolve @p local_irq_id on @p cpu_id to a global_irq_id, or INVALID. */
size_t arch_map_global_irq_id(cpu_id_t cpu_id, size_t local_irq_id);

/** True if @p local_irq_id is free to claim on @p cpu_id. */
bool arch_local_irq_id_available(cpu_id_t cpu_id, size_t local_irq_id);

/**
 * Try a direct local_irq_id for @p global_irq_id (arch formula / 1:1).
 * @return the local_irq_id, or @c IRQ_LOCAL_ID_INVALID if this arch has no
 *         direct map — @c irq_global_alloc then scans for a free local id.
 */
size_t arch_try_direct_map_local_irq_id(size_t global_irq_id);

/**
 * Establish or clear the local_irq_id → global_irq_id map on @p cpu_id.
 * @p global_irq_id @c IRQ_GLOBAL_ID_INVALID clears the mapping.
 * @return false if the update fails (e.g. already mapped).
 */
bool arch_map_local_irq_id(cpu_id_t cpu_id, size_t local_irq_id,
                           size_t global_irq_id);

/** Reset arch-private fields of an unused global irq entry. */
void arch_irq_global_entry_reset(struct arch_global_irq *entry);

/**
 * Allocate @c global_irq_vector of @p count entries and set @c nr_global_irq.
 *
 * Called once from @c arch_start_platform on the BSP, after @c kallocator
 * exists. @p count is the platform external-irq count. Entries start unused;
 * id fields are @c IRQ_LOCAL_ID_INVALID / @c CPU_ID_INVALID.
 *
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if @p count is 0,
 *         @c -E_RENDEZVOS if already allocated or @c kallocator fails.
 */
error_t irq_global_vector_alloc(size_t count);

/**
 * Claim @p global_irq_id and map a local_irq_id on @p cpu_id (concrete).
 *
 * Flow: try @c arch_try_direct_map_local_irq_id first. If the arch accepts
 * a direct map, use that local_irq_id. If not (@c IRQ_LOCAL_ID_INVALID —
 * e.g. no formula on x86), scan for a free local_irq_id on @p cpu_id.
 * Ownership is CAS on the entry's @c irq_attr (@c IRQ_ATTR_USED). On map
 * failure the claim is rolled back.
 * @p local_irq_id_out receives the id for @c register_irq_handler.
 *
 * @return @c REND_SUCCESS,
 *         @c -E_IN_PARAM on bad arguments,
 *         @c -E_REND_AGAIN if the entry is busy or the map update raced,
 *         @c -E_REND_OVERFLOW if no free local_irq_id remains.
 */
error_t irq_global_alloc(size_t global_irq_id, cpu_id_t cpu_id,
                         size_t *local_irq_id_out);

/**
 * Release @p global_irq_id previously claimed for @p cpu_id (concrete).
 *
 * Must run on the owning CPU (@p cpu_id == current). Clears the per-CPU map
 * binding and affinity fields, resets arch-private fields, then drops
 * @c IRQ_ATTR_USED via @c irq_atomic_attr_try_set_unused (preserves
 * @c IRQ_NEED_EOI and leaves @c irq_handler unchanged for migrate-without-
 * re-register). Mask / stop delivery before calling.
 *
 * @return @c REND_SUCCESS,
 *         @c -E_IN_PARAM on bad arguments or caller is not @p cpu_id,
 *         @c -E_REND_NOFOUND if not claimed or affinity mismatch.
 */
error_t irq_global_free(size_t global_irq_id, cpu_id_t cpu_id);

#endif /* _RENDEZVOS_IRQ_SPACE_H_ */
