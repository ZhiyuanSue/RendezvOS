#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Two ids, matching the two arrays:
 * - local_irq_id: per-CPU id. Below @c NR_ARCH_LOCAL_IRQ it indexes
 *   @c local_irq_vector. At or above that limit it maps to a global_irq_id.
 * - global_irq_id: index of @c global_irq_vector.
 *
 * Affinity: one global irq owns one (local_irq_id, cpu_id). Remap =
 * free then alloc.
 *
 * @c CPU_ID_INVALID:
 * - map / find APIs that take a cpu_id: means the current CPU.
 * - alloc / free / register_irq_handler: require a concrete cpu_id.
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
 * Allocated once in @c arch_start_platform via @c irq_global_vector_alloc.
 * @c nr_global_irq is the length of that vector.
 */
extern struct global_irq *global_irq_vector;
extern size_t nr_global_irq;

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

typedef void (*fixed_trap_handler_t)(struct trap_frame *tf);

bool irq_trap_is_local(size_t local_irq_id);

/**
 * Look up the global irq entry for @p local_irq_id on @p cpu_id.
 * @p cpu_id @c CPU_ID_INVALID means the current CPU.
 * @return the entry, or NULL if not allocated / affinity mismatch.
 */
struct global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id);

/* === arch map helpers (arch implements) === */
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
 * Fixed local_irq_id for @p global_irq_id, or @c IRQ_LOCAL_ID_INVALID when
 * alloc must pick a free local_irq_id on the destination CPU.
 */
size_t arch_map_fixed_local_irq_id(size_t global_irq_id);

/**
 * Establish or clear the local_irq_id → global_irq_id map on @p cpu_id.
 * @p global_irq_id @c IRQ_GLOBAL_ID_INVALID clears the mapping.
 * @return false if the update fails (e.g. already mapped).
 */
bool arch_map_local_irq_id(cpu_id_t cpu_id, size_t local_irq_id,
                           size_t global_irq_id);

/** Reset arch-private fields of an unused global irq entry. */
void arch_irq_global_entry_reset(struct global_irq *entry);

void arch_eoi_irq(u64 trap_info);

void arch_register_fixed_trap(enum trap_class trap_class,
                              fixed_trap_handler_t handler, u64 irq_attr);

/**
 * Install @p handler on @p local_irq_id for @p cpu_id (concrete).
 * Local: writes @c local_irq_vector. Global: requires a prior
 * @c irq_global_alloc that owns this (local_irq_id, cpu_id).
 */
void register_irq_handler(size_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);

void init_interrupt(void);

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
 * Clears the map, resets the entry, then clears @c IRQ_ATTR_USED last.
 *
 * @return @c REND_SUCCESS,
 *         @c -E_IN_PARAM on bad arguments,
 *         @c -E_REND_NOFOUND if not claimed or affinity mismatch.
 */
error_t irq_global_free(size_t global_irq_id, cpu_id_t cpu_id);

#endif
