#ifndef _RENDEZVOS_IRQ_SPACE_COMMON_H_
#define _RENDEZVOS_IRQ_SPACE_COMMON_H_

#include <common/stddef.h>
#include <common/types.h>
#include <common/bit.h>
#include <common/atomic.h>
#include <rendezvos/smp/cpu_id.h>

/* Forward declaration (defined in arch-specific trap.h) */
struct trap_frame;
/* Defined in irq_space.h after arch entry type is known. */
struct irq_space;

/**
 * Software irq number.
 */
typedef size_t local_irq_id_t;
typedef size_t global_irq_id_t;

/**
 * Fields shared by every arch's @c struct arch_global_irq.
 * Arch headers embed this macro, then append arch-only fields (controller
 * topology). @c space selects how enable/disable/eoi program hardware.
 * Machine-wide @c global_irq_id is not stored here — it is the registry /
 * space span coordinate used at alloc/lookup time.
 *
 * Entry lifetime (recyclable slot; memory freed only with the space):
 *   @c IRQ_ATTR_USED — alloc admission (CAS). Owner-exclusive ops apply from
 *   line enable until free; before enable, other CPUs may touch soft state
 *   (migrate). No per-entry refcount; free keeps @c irq_handler.
 */
#define GLOBAL_IRQ_COMMON                                              \
        void (*irq_handler)(struct trap_frame * tf);                   \
        atomic64_t irq_attr; /* CAS USED at alloc; NEED_EOI, … */    \
        local_irq_id_t local_irq_id; /* per-CPU id mapped on cpu_id */ \
        cpu_id_t cpu_id;                                               \
        struct irq_space *space;

#define IRQ_NO_ATTR (0ull)
/* trap_handler need EOI after irq_handler returns */
#define IRQ_NEED_EOI BIT_U64(0)
/* this irq has been used */
#define IRQ_ATTR_USED BIT_U64(1)

/* No valid global_irq_id (not owned by any registered space range). */
#define IRQ_GLOBAL_ID_INVALID ((global_irq_id_t)-1)
/* No valid local_irq_id, or entry unset. */
#define IRQ_LOCAL_ID_INVALID ((local_irq_id_t)-1)

#endif
