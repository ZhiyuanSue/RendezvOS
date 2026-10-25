#ifndef _RENDEZVOS_X86_64_IRQ_SPACE_H_
#define _RENDEZVOS_X86_64_IRQ_SPACE_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/trap_common.h> /* GLOBAL_IRQ_COMMON */

/*
 * Arch piece of irq_space (software ids + per-CPU map). Does not touch
 * IOAPIC/LAPIC programming — only how software numbers relate to GSI and
 * the 256-wide IDT vector line (@c NR_IDT_VECTORS). Map code:
 * @c arch/x86_64/trap/irq_space.c.
 *
 * LOCAL: [0, ALLOC_LO) — exceptions + APIC LVT/IPI (structural; no reserve).
 * GLOBAL: one entry per global_irq_id (GSI). local_irq_id → global_irq_id
 * via the per-CPU map below. IDT is always @c NR_IDT_VECTORS (256).
 *
 * Each CPU has the same local_irq_id number line. Low ids live in
 * local_irq_vector[]; high ids are virtual (not in that vector) and map into
 * the shared global_irq_vector[].
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
/*
 * Temporary global_irq_id count until MADT is parsed (GSI count).
 * @c arch_start_platform passes this to @c irq_global_vector_alloc.
 */
#define NR_ARCH_GLOBAL_IRQ 256
#define NR_IDT_VECTORS     256
#define NR_IRQ             NR_IDT_VECTORS

/**
 * Arch global external irq entry. Index is @c global_irq_id (GSI).
 * Dispatch: local_irq_id → per-CPU map → @c global_irq_vector[global_irq_id].
 * Extra fields: IOAPIC pin, filled when the route is programmed.
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
