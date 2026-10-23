#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Trap dispatch: @c trap_handler, @c register_irq_handler, @c init_interrupt,
 * fixed traps, EOI. Software irq id alloc/map: @c rendezvos/trap/irq_space.h
 * (included below so existing callers keep one include).
 *
 * @c CPU_ID_INVALID on @c register_irq_handler: not allowed (concrete cpu_id).
 * Global register: caller must be the owning CPU (see irq_space ownership).
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

typedef void (*fixed_trap_handler_t)(struct trap_frame *tf);

void arch_eoi_irq(u64 trap_info);

void arch_register_fixed_trap(enum trap_class trap_class,
                              fixed_trap_handler_t handler, u64 irq_attr);

/**
 * Install @p handler on @p local_irq_id for @p cpu_id (concrete).
 *
 * Local: writes that CPU's @c local_irq_vector slot (any caller may target
 * another CPU — used by fixed-trap bring-up).
 *
 * Global: requires a prior @c irq_global_alloc for this (local_irq_id,
 * cpu_id), and the caller must be that owning CPU. @p handler may be NULL
 * to clear the ISR (e.g. before module unload). Migrate without changing
 * the ISR does not need another register — free keeps the pointer.
 */
void register_irq_handler(size_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);

void init_interrupt(void);

void trap_handler(struct trap_frame *tf);

#endif
