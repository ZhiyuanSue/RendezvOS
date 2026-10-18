/*
 * Common trap definitions shared across all architectures.
 *
 * This header must NOT include any architecture-specific headers
 * to avoid circular dependencies.
 *
 * Included by rendezvous/trap.h and architecture-specific trap headers.
 */
#ifndef _RENDEZVOS_TRAP_COMMON_H_
#define _RENDEZVOS_TRAP_COMMON_H_

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
 * (CAS claim during @c irq_delivery_alloc).
 */
#define IRQ_COMMON \
        void (*irq_handler)(struct trap_frame *tf);

#define IRQ_NO_ATTR  (0ull)
#define IRQ_NEED_EOI BIT_U64(0) /* @c trap_handler need EOI after irq_handler return */
#define IRQ_VEC_USED BIT_U64(1) /* this irq have been used */

/**
 * Sentinels (same bit pattern, different roles — do not conflate in comments):
 * - @c IRQ_TABLE_INDEX_INVALID: no row in @c global_irq_table
 * - @c IRQ_HWIRQ_INVALID: no hardware irq number (GSI / INTID) yet
 */
#define IRQ_TABLE_INDEX_INVALID ((u32)-1)
#define IRQ_HWIRQ_INVALID       ((u32)-1)

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
        TRAP_CLASS_SEGMENT_FAULT, /* Segment-related faults (x86: invalid TSS,
                                     segment not present) */
        TRAP_CLASS_SECURITY, /* Security exception (x86 #SE, ARM MTE fault) */
        TRAP_CLASS_VIRTUALIZATION, /* Virtualization exception (x86 #VE, EPT
                                      violations) */
        TRAP_CLASS_ASYNC_ABORT, /* Asynchronous abort (ARM SError, external
                                   abort) */
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
 * Core/ provides TWO levels of handler registration:
 *
 * 1. Fixed trap handlers (architecture-independent):
 *    - arch_register_fixed_trap(trap_class, handler, irq_attr)
 *    - One handler per trap_class (not per trap_id)
 *    - Multiple trap_ids can map to the same trap_class
 *    - Example: x86 #PF (14) and ARM data abort (0x24) both use
 * TRAP_CLASS_PAGE_FAULT
 *
 * 2. Direct IRQ handlers (architecture-specific):
 *    - register_irq_handler(delivery_id, dest_cpu, handler, irq_attr)
 *    - Local: one slot per (delivery_id, cpu); global: one row per
 *      (delivery_id, dest_cpu) from irq_delivery_alloc
 *
 * PRIORITY AND MUTUAL EXCLUSION:
 *
 * For each (delivery_id, cpu) local slot / global row:
 *   - ONLY ONE handler at a time; last registration wins
 *   - Mixing fixed_trap and direct register for the same slot is NOT recommended
 *
 * Recommended Usage:
 *   ✅ DO: arch_register_fixed_trap(TRAP_CLASS_PAGE_FAULT, handle_pf, IRQ_NEED_EOI);
 *   ✅ DO: register_irq_handler(14, cpu, x86_specific_pf_handler, 0);
 *
 *   ❌ DON'T: Mix both for the same local delivery_id on the same cpu
 *
 * Implementation Detail:
 *   - arch_register_fixed_trap() calls register_irq_handler(id, cpu, ...) for
 *     each CPU and each arch delivery_id mapped to the trap_class
 *
 * ========================================================================
 */

#endif
