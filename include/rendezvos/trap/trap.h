#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Include architecture-specific trap headers.
 * This provides:
 * - struct trap_frame (architecture-specific)
 * - Architecture-specific trap macros
 */
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
#define IRQ_NO_ATTR  (0)
#define IRQ_NEED_EOI (1)
        u64 irq_attr;
};

extern struct irq irq_handler[NR_IRQ];
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *tf),
                          u64 irq_attr);
void init_interrupt(void);
void arch_eoi_irq(u64 trap_info);

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
