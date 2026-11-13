#ifndef _RENDEZVOS_IRQ_SPACE_H_
#define _RENDEZVOS_IRQ_SPACE_H_

/*
 * irq_space — software irq numbering (common layer).
 * INCLUDING:
 *
 *   1) local irq wrappers
 *   2) irq_space registry — register / unregister (cold; state machine life)
 *   3) global irq claim — alloc / free by gid; find by local
 *
 * Refcount ownership (transfer, not dual-hold):
 *   create     → refcount 1 (creator)
 *   register   → transfer that 1 to the registry list (still 1; creator drops
 *                logical ownership; no extra get)
 *   ops_begin  → ref_get_not_zero while REGISTERED (in-flight alloc/free)
 *   ops_end    → ref_put one.
 *   unregister → CLOSING while linked; idle means refcount==1 (list only hold
 *             refcount); unlink + CLOSED transfers that 1 to the unregisterer,
 *             who ref_put → free. Busy (refcount!=1 or nr_used!=0) → restore
 *             REGISTERED, stay linked, -E_REND_AGAIN.
 *
 * Global entry (see @c GLOBAL_IRQ_COMMON): USED admits alloc. No per-entry
 * refcount. @c nr_used tracks claimed slots (inc on alloc, dec when USED
 * cleared on free). 
 * 
 * Ownership window:
 *   After line @c enable until @c free completes — only the owner CPU
 *   (@c cpu_id) may operate the entry (disable/free/register/delivery).
 *   Before enable (claim exists but line masked) — other CPUs may update
 *   soft state (e.g. migrate: old owner alloc+enable for the new affinity);
 *   no IRQ delivery yet so that is safe. @c irq_global_free keeps
 *   @c irq_handler for migrate without re-register.
 */
#include <common/bit.h>
#include <common/stdbool.h>
#include <common/atomic.h>
#include <common/refcount.h>
#include <common/dsa/list.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/irq_space_common.h>
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

/* === local irq === */

struct local_irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr; /* plain store; per-CPU, no CAS */
};

extern struct local_irq local_irq_vector[NR_ARCH_LOCAL_IRQ];

bool irq_trap_is_local(local_irq_id_t local_irq_id);
struct local_irq *irq_local_find(local_irq_id_t local_irq_id, cpu_id_t cpu_id);

/* === irq space === */

#define IRQ_SPACE_LIFE_CLOSED     0
#define IRQ_SPACE_LIFE_ACTIVE     1 /* created, not on the registry list */
#define IRQ_SPACE_LIFE_REGISTERED 2
#define IRQ_SPACE_LIFE_CLOSING    3

struct irq_space {
        /*for multi spaces*/
        struct list_entry node;
        /*data*/
        void *priv;
        /*global irq id ranges*/
        global_irq_id_t global_irq_id_base;
        size_t nr_global_irq_id;
        atomic64_t nr_used; /* claimed (!idle) entries; 0 before unregister */

        ref_count_t refcount; /* list(1) + in-flight ops; idle registered == 1
                               */
        atomic64_t life; /* @c IRQ_SPACE_LIFE_* */

        struct arch_global_irq *(*lookup_global_irq_id)(
                struct irq_space *space, global_irq_id_t global_irq_id);
        local_irq_id_t (*choose_local)(struct irq_space *space,
                                       global_irq_id_t global_irq_id,
                                       cpu_id_t cpu_id);
        bool (*bind_local)(struct irq_space *space, cpu_id_t cpu_id,
                           local_irq_id_t local_irq_id,
                           struct arch_global_irq *entry);
        error_t (*enable)(struct arch_global_irq *entry,
                          enum irq_trigger trigger, enum irq_polarity polarity);
        error_t (*disable)(struct arch_global_irq *entry);
        void (*eoi)(struct arch_global_irq *entry, u64 trap_info);
        void (*destroy_priv)(struct irq_space *space);
};

static inline bool irq_space_covers(const struct irq_space *space,
                                    global_irq_id_t global_irq_id)
{
        return global_irq_id >= space->global_irq_id_base
               && global_irq_id
                          < space->global_irq_id_base
                                    + (global_irq_id_t)space->nr_global_irq_id;
}

/** Read @c life (port @c port_ops_life_get). */
static inline i64 irq_space_life_load(const struct irq_space *space)
{
        return (i64)atomic64_load(
                (volatile const u64 *)&space->life.counter);
}

/** CAS @c life expect→target (port @c port_ops_set_life_with_expect). */
static inline bool irq_space_set_life_with_expect(struct irq_space *space,
                                                  i64 expect, i64 target)
{
        return atomic64_cas((volatile u64 *)&space->life.counter,
                            (u64)expect,
                            (u64)target)
               == (u64)expect;
}

/**
 * Cold critical section: pin @c refcount while life is REGISTERED.
 * Pair with @c irq_space_ops_end (@c ref_put, no free).
 */
static inline bool irq_space_ops_begin(struct irq_space *space)
{
        if (atomic64_load((volatile const u64 *)&space->life.counter)
            != IRQ_SPACE_LIFE_REGISTERED)
                return false;
        if (!ref_get_not_zero(&space->refcount))
                return false;
        if (atomic64_load((volatile const u64 *)&space->life.counter)
            != IRQ_SPACE_LIFE_REGISTERED) {
                ref_put(&space->refcount, NULL);
                return false;
        }
        return true;
}

static inline void irq_space_ops_end(struct irq_space *space)
{
        ref_put(&space->refcount, NULL);
}

/** Backend create-time: ACTIVE life, creator holds refcount 1, nr_used 0. */
static inline void irq_space_common_init(struct irq_space *space)
{
        INIT_LIST_HEAD(&space->node);
        ref_init(&space->refcount);
        atomic64_init(&space->life, IRQ_SPACE_LIFE_ACTIVE);
        atomic64_init(&space->nr_used, 0);
}

error_t irq_space_register(struct irq_space *space);
error_t irq_space_unregister(struct irq_space *space);
error_t free_irq_space_ref(ref_count_t *ref_count_ptr);

/* === global irq attr / entry === */

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

static inline bool irq_atomic_attr_try_set_used(atomic64_t *attr)
{
        u64 old_attr = irq_atomic_attr_load(attr);
        if (irq_attr_used(old_attr))
                return false;
        u64 new_attr = irq_attr_set_used(old_attr);
        return atomic64_cas((volatile u64 *)&attr->counter, old_attr, new_attr)
               == old_attr;
}

static inline bool irq_atomic_attr_try_set_unused(atomic64_t *attr)
{
        u64 old_attr = irq_atomic_attr_load(attr);

        if (!irq_attr_used(old_attr))
                return false;
        u64 new_attr = irq_attr_set_unused(old_attr);
        return atomic64_cas((volatile u64 *)&attr->counter, old_attr, new_attr)
               == old_attr;
}

static inline void irq_global_entry_common_init(struct arch_global_irq *entry,
                                                struct irq_space *space)
{
        atomic64_init(&entry->irq_attr, 0);
        entry->local_irq_id = IRQ_LOCAL_ID_INVALID;
        entry->cpu_id = CPU_ID_INVALID;
        entry->irq_handler = NULL;
        entry->space = space;
}

error_t irq_global_find(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                        struct arch_global_irq **entry_out);
error_t irq_global_alloc(global_irq_id_t global_irq_id, cpu_id_t cpu_id,
                         local_irq_id_t *local_irq_id_out);
/** Owner-CPU unclaim: unbind, clear USED, nr_used-- (not entry memory free). */
error_t irq_global_free(global_irq_id_t global_irq_id);

#endif /* _RENDEZVOS_IRQ_SPACE_H_ */
