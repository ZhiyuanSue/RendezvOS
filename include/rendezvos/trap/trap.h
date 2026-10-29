#ifndef _RENDEZVOS_TRAP_H_
#define _RENDEZVOS_TRAP_H_

/*
 * Trap: CPU exception/IRQ entry, handler install, and (later) irq
 * enable/migrate.
 *
 * Who calls whom:
 *
 *              ┌─────────────────────────────────┐
 *              │ trap  (this module)             │
 *              └───────────────┬─────────────────┘
 *                 ┌────────────┴────────────┐
 *                 ▼                         ▼
 *        ┌────────────────┐       ┌─────────────────────────┐
 *        │ irq_space      │◄──────│ arch irqchip module     │
 *        │                │       │ x86 IOAPIC /            │
 *        │                │       │ aarch64 GIC             │
 *        └────────────────┘       └─────────────────────────┘
 *
 * Two jobs (do not conflate):
 *
 * (A) When an interrupt already hit the CPU  —  @c trap_handler path
 *     IDT/vector → trap_handler → look up irq_space → call handler → EOI
 *     Uses irq_space (+ arch EOI) only. Does not program IOAPIC/GIC routes.
 *     @c register_irq_handler stores the ISR into irq_space.
 *
 * (B) Turn a line on/off or move it to another CPU  —  mostly TODO
 *     Example enable: irq_global_alloc + register_irq_handler + write RTE
 *     Public API will live here; body calls irq_space and arch irqchip.
 *     Today @c ioapic_init already shows irqchip → irq_space at boot.
 *
 * This common header includes irq_space only — never IOAPIC/GIC headers.
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
void register_irq_handler(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *tf), u64 irq_attr);

void init_interrupt(void);

void trap_handler(struct trap_frame *tf);

#endif
