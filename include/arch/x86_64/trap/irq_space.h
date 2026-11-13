#ifndef _RENDEZVOS_X86_64_IRQ_SPACE_H_
#define _RENDEZVOS_X86_64_IRQ_SPACE_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/irq_space_common.h> /* GLOBAL_IRQ_COMMON */

/*
 * x86_64 irq_space: software local/global ids and the per-CPU map.
 * Implementation: @c arch/x86_64/trap/irq_space.c.
 *
 * This header must not include PIC/controller headers (IOAPIC, etc.): it only
 * describes software numbering. On x86, today's IOAPIC space uses
 * global_irq_id == GSI with base 0; hardware programming uses entry fields.
 *
 * LOCAL:  [0, ALLOC_LO) — exceptions + APIC LVT/IPI (in local_irq_vector[]).
 * GLOBAL: per-space entry tables; local_irq_id → global_irq_id via the
 *         per-CPU map below. IDT width is @c NR_IDT_VECTORS (256).
 *
 * Each CPU shares the same local_irq_id number line. Low ids are real local
 * slots; high ids are virtual and map into a registered space's entries[].
 *
 *   CPU0 local_irq_vector[] (real, private)     virtual ids (not in local)
 *   +-----+-----+-----+-----+                   +-----+-------+-----+
 *   |  0  |  1  | ... |LO-1 |                   | LO  | LO+1  | ... |
 *   +-----+-----+-----+-----+                   +--+--+--+----+-----+
 *                                                  |
 *                                                  v
 *                    irq_space (e.g. IOAPIC) entries[] by global_irq_id
 *
 * Same virtual id LO on CPU0 and CPU1 can map to different global ids.
 * LO = ARCH_IRQ_ID_ALLOC_LO.
 */
#define ARCH_IRQ_ID_TIMER    0x20u
#define ARCH_IRQ_ID_SPURIOUS 0x27u
#define ARCH_IRQ_ID_IPI      0x30u
#define ARCH_IRQ_ID_ALLOC_LO 0x40u
#define ARCH_IRQ_ID_ALLOC_HI 0xEFu

#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO

#define NR_IDT_VECTORS 256
#define NR_IRQ         NR_IDT_VECTORS

/* Defined in PIC/IOAPIC.h */
struct ioapic;

/**
 * Arch global external irq entry. @c global_irq_id is the machine-wide id
 * (IOAPIC space: GSI). @c ioapic caches the chip for EOI; GSI for
 * enable/disable is the entry index in @c space->priv (+ base).
 * Dispatch: local_irq_id → per-CPU map → space entry.
 * Topology filled by @c irq_space_ioapic_register.
 */
struct arch_global_irq {
        GLOBAL_IRQ_COMMON
        struct ioapic *ioapic; /* EOI hot path; NULL if GSI unbound at init */
};

/*
 * Shared IDT delivery helpers for all x86 spaces (IOAPIC / MSI / …).
 * Implementation: @c arch/x86_64/trap/irq_space.c (per-CPU map of entry *).
 * Hot find: @c arch_irq_entry_from_local.
 *
 * Per-controller space bring-up lives elsewhere, e.g.
 * @c irq_space_ioapic.c — not in this file (MSI will get its own).
 */
struct arch_global_irq *arch_irq_entry_from_local(cpu_id_t cpu_id,
                                                  local_irq_id_t local_irq_id);
local_irq_id_t arch_irq_space_choose_local(struct irq_space *space,
                                           global_irq_id_t global_irq_id,
                                           cpu_id_t cpu_id);
bool arch_irq_space_bind_local(struct irq_space *space, cpu_id_t cpu_id,
                               local_irq_id_t local_irq_id,
                               struct arch_global_irq *entry);

/** Register the IOAPIC GSI irq_space (after @c ioapic probe). */
error_t irq_space_ioapic_register(void);

#endif /* _RENDEZVOS_X86_64_IRQ_SPACE_H_ */
