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
 * This header must not include PIC/irqchip headers (IOAPIC, etc.): it only
 * describes software numbering. On x86, global_irq_id equals @c gsi_id_t
 * (@c arch/x86_64/PIC/gsi.h); chip/ISO/RTE stay under PIC/.
 *
 * LOCAL:  [0, ALLOC_LO) — exceptions + APIC LVT/IPI (in local_irq_vector[]).
 * GLOBAL: one slot per global_irq_id; local_irq_id → global_irq_id via the
 *         per-CPU map below. IDT width is @c NR_IDT_VECTORS (256).
 *
 * Each CPU shares the same local_irq_id number line. Low ids are real local
 * slots; high ids are virtual and map into the shared global_irq_vector[].
 *
 *   CPU0 local_irq_vector[] (real, private)     virtual ids (not in local)
 *   +-----+-----+-----+-----+                   +-----+-------+-----+
 *   |  0  |  1  | ... |LO-1 |                   | LO  | LO+1  | ... |
 *   +-----+-----+-----+-----+                   +--+--+--+----+-----+
 *                                                  |     |
 *                                                  |     +-----> (unmapped)
 *                                                  v
 *   CPU1 local_irq_vector[] (real, private)     virtual ids
 *   +-----+-----+-----+-----+                   +-----+-------+-----+
 *   |  0  |  1  | ... |LO-1 |                   | LO  | LO+1  | ... |
 *   +-----+-----+-----+-----+                   +--+--+--+----+-----+
 *                                                  |
 *                                                  v
 *                    shared global_irq_vector[] (real, machine-wide)
 *                    +------+------+-----+------+-----+-------+
 *                    | g[0] | g[1] | ... | g[j] | ... | g[k]  | ...
 *                    +------+------+-----+--+---+-----+--+----+
 *                                          ^             ^
 *                                          |             |
 *                               CPU0 LO ---+             |
 *                               CPU1 LO -----------------+
 *
 * Same virtual id LO on CPU0 and CPU1 can map to different g[i].
 * LO = ARCH_IRQ_ID_ALLOC_LO; g[i] = global_irq_vector[i].
 */
#define ARCH_IRQ_ID_TIMER    0x20u
#define ARCH_IRQ_ID_SPURIOUS 0x27u
#define ARCH_IRQ_ID_IPI      0x30u
#define ARCH_IRQ_ID_ALLOC_LO 0x40u
#define ARCH_IRQ_ID_ALLOC_HI 0xEFu

#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO

#define NR_IDT_VECTORS 256
#define NR_IRQ         NR_IDT_VECTORS

/**
 * Arch global external irq entry. Index is @c global_irq_id (== GSI).
 * Dispatch: local_irq_id → per-CPU map → @c global_irq_vector[global_irq_id].
 * @c ioapic_id / @c pin filled by @c ioapic_init (not by this module).
 */
struct arch_global_irq {
        GLOBAL_IRQ_COMMON
        u8 ioapic_id;
        u8 pin;
};

/*
 * Per-CPU map @c local_irq_map[] (u64, CAS): local_irq_id → global_irq_id+1
 * (0 = not mapped). Alloc: CAS irq_attr on the chosen global_irq_id, then
 * CAS this map on cpu_id only. @c arch_choose_local_irq_id scans
 * [ALLOC_LO, ALLOC_HI] for a free id.
 * Implementation: @c arch/x86_64/trap/irq_space.c.
 */

#endif /* _RENDEZVOS_X86_64_IRQ_SPACE_H_ */
