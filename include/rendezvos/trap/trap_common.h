/*
 * Common trap definitions shared across all architectures.
 *
 * This header must NOT include any architecture-specific headers
 * to avoid circular dependencies.
 *
 * Included by rendezvos/trap.h and architecture-specific trap headers.
 */
#ifndef _RENDEZVOS_TRAP_COMMON_H_
#define _RENDEZVOS_TRAP_COMMON_H_

#include <common/stddef.h>
#include <common/types.h>
#include <common/stdbool.h>
#include <common/bit.h>
#include <common/mm.h> /* vaddr */
#include <common/atomic.h>

/* Forward declaration (defined in arch-specific trap.h) */
struct trap_frame;

/**
 * Shared handler pointer for @c local_irq / @c global_irq.
 * Attr storage differs: local uses plain @c u64; global uses @c atomic64_t
 * (CAS claim during @c irq_global_alloc).
 */
#define IRQ_COMMON \
        void (*irq_handler)(struct trap_frame *tf);

#define IRQ_NO_ATTR   (0ull)
#define IRQ_NEED_EOI  BIT_U64(0) /* trap_handler EOI after irq_handler returns */
#define IRQ_ATTR_USED BIT_U64(1) /* this irq has been claimed / is in use */

/** No global_irq_id (not an index into @c global_irq_vector). */
#define IRQ_GLOBAL_ID_INVALID ((size_t)-1)
/** No fixed local_irq_id for this global irq; alloc picks one. */
#define IRQ_LOCAL_ID_INVALID  ((size_t)-1)

/*
 * TRAP_COMMON macro (similar to PMM_COMMON pattern).
 *
 * Each architecture's trap_info structure must include this macro.
 * Provides architecture-independent trap classification and information.
 *
 * Includes reference to trap_frame for accessing raw hardware registers
 * without duplicating error_code, ESR, FAR, etc.
 *
 * Uses bitfields to minimize structure size.
 */
#define TRAP_COMMON                                             \
        /* Reference to hardware state (not duplicated) */      \
        struct trap_frame *tf;                                  \
                                                                \
        /* Basic information (using bitfields to save space) */ \
        u8 trap_class;                                          \
        u8 is_user : 1;                                         \
        u8 is_fatal : 1;                                        \
        u8 reserved1 : 6;                                       \
                                                                \
        /* Page fault common fields */                          \
        vaddr fault_addr;                                       \
        u8 is_write : 1;                                        \
        u8 is_execute : 1;                                      \
        u8 is_present : 1;                                      \
        u8 reserved2 : 5;                                       \
                                                                \
        /* Other common fields */                               \
        u32 error_code;                                         \
        u64 arch_flags;                                         \
                                                                \
        /* Reserved for future extension */                     \
        u64 reserved[4];

/**
 * @brief Trap classification (architecture-independent)
 *
 * Used for upper layer handlers to process traps by semantic type,
 * not architecture-specific trap IDs.
 *
 * Each class represents a category of exceptions with similar semantics
 * across architectures, even if the underlying hardware mechanisms differ.
 *
 * IMPORTANT: TRAP_CLASS_UNKNOWN must be the last entry as it defines
 * the array size for fixed_trap_handlers[].
 */
enum trap_class {
        TRAP_CLASS_PAGE_FAULT, /* Memory access fault: page not present or
                                  protection violation */
        TRAP_CLASS_ILLEGAL_INSTR, /* Invalid or undefined instruction */
        TRAP_CLASS_BREAKPOINT, /* Breakpoint hit (debugger) */
        TRAP_CLASS_ALIGNMENT, /* Unaligned access (may be emulated on some
                                 architectures) */
        TRAP_CLASS_DIVIDE_ERROR, /* Division by zero or integer overflow */
        TRAP_CLASS_OVERFLOW, /* Arithmetic overflow (bounded integer operations)
                              */
        TRAP_CLASS_FP_FAULT, /* Floating point exception */
        TRAP_CLASS_GP_FAULT, /* General protection fault (privilege or access
                                violation) */
        TRAP_CLASS_STACK_FAULT, /* Stack corruption or limit violation */
        TRAP_CLASS_MACHINE_CHECK, /* Hardware error or corruption */
        TRAP_CLASS_SYSCALL, /* System call entry */
        TRAP_CLASS_IRQ, /* External device interrupt */
        TRAP_CLASS_DEBUG, /* Debug exception (single-step, breakpoint) */
        TRAP_CLASS_DOUBLE_FAULT, /* Double fault (critical error indicating
                                    handler bug) */
        TRAP_CLASS_SEGMENT_FAULT, /* Segment-related faults */
        TRAP_CLASS_SECURITY, /* Security exception */
        TRAP_CLASS_VIRTUALIZATION, /* Virtualization exception */
        TRAP_CLASS_ASYNC_ABORT, /* Asynchronous abort / external abort */
        TRAP_CLASS_UNKNOWN, /* Unknown or unsupported exception (MUST BE LAST)
                             */
};

/*
 * Compile-time check: Ensure trap_class fits in u8.
 *
 * This check ensures that the number of trap classes doesn't exceed 255,
 * which is the maximum value that can be stored in a u8 type.
 *
 * If you add new trap_class entries and this check fails, you need to:
 * 1. Change trap_class in TRAP_COMMON from u8 to u16, OR
 * 2. Reduce the number of trap_class entries
 *
 * Note: We use an array size check instead of static_assert because
 * this codebase may not have static_assert implemented.
 */
#define TRAP_CLASS_U8_BOUNDARY_CHECK       \
        _Static_assert_or_size_check(      \
                TRAP_CLASS_UNKNOWN <= 255, \
                "trap_class exceeds u8 boundary; change u8 to u16 in TRAP_COMMON")

/*
 * Portable compile-time check macro (works without static_assert).
 * If the array size is negative, it will cause a compilation error.
 */
#define _Static_assert_or_size_check(cond, msg) \
        struct _trap_class_check {              \
                char _array[(cond) ? 1 : -1];   \
        }

/* Instantiate the check (this will fail to compile if TRAP_CLASS_UNKNOWN > 255)
 */
TRAP_CLASS_U8_BOUNDARY_CHECK;

/*
 * ========================================================================
 * Handler Registration Priority and Mutual Exclusion
 * ========================================================================
 *
 * Core provides two registration levels:
 *
 * 1. Fixed trap handlers:
 *    - arch_register_fixed_trap(trap_class, handler, irq_attr)
 *    - One handler per trap_class (not per local_irq_id)
 *    - Multiple local_irq_ids may map to the same trap_class
 *
 * 2. Direct IRQ handlers:
 *    - register_irq_handler(local_irq_id, cpu_id, handler, irq_attr)
 *    - Local: one entry per (local_irq_id, cpu_id)
 *    - Global: one entry per (local_irq_id, cpu_id) from irq_global_alloc
 *
 * PRIORITY AND MUTUAL EXCLUSION:
 *
 * For each (local_irq_id, cpu_id) local / global entry:
 *   - Only one handler at a time; last registration wins
 *   - Mixing fixed_trap and direct register for the same entry is not
 *     recommended
 *
 * Recommended usage:
 *   - arch_register_fixed_trap(TRAP_CLASS_PAGE_FAULT, handle_pf, IRQ_NEED_EOI);
 *   - register_irq_handler(local_irq_id, cpu_id, handler, IRQ_NO_ATTR);
 *
 * Do not mix both for the same local_irq_id on the same cpu_id.
 *
 * Implementation note:
 *   arch_register_fixed_trap() calls register_irq_handler() for each CPU and
 *   each local_irq_id that belongs to the trap_class.
 *
 * ========================================================================
 */

#endif
