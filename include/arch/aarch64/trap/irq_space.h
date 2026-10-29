#ifndef _RENDEZVOS_AARCH64_IRQ_SPACE_H_
#define _RENDEZVOS_AARCH64_IRQ_SPACE_H_
#include <common/bit.h>
#include <common/types.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/irq_space_common.h>
#include "trap_def.h"

#define AARCH64_IRQ_TO_TRAP_ID(irq_number) (irq_number + AARCH64_IRQ_OFFSET)
#define AARCH64_TRAP_ID_TO_IRQ(trap_id)    (trap_id - AARCH64_IRQ_OFFSET)

/*
 * Arch piece of irq_space: software local/global ids vs GIC INTID (macros
 * above). Does not program the GIC distributor/CPU interface. Map helpers:
 * @c arch/aarch64/trap/irq_space.c.
 */
#define ARCH_IRQ_INTID_IPI   GIC_V2_SGI_START /* 0 */
#define ARCH_IRQ_ID_IPI      AARCH64_IRQ_TO_TRAP_ID(ARCH_IRQ_INTID_IPI)
#define ARCH_IRQ_ID_ALLOC_LO AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_START)
#define ARCH_IRQ_ID_ALLOC_HI AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_END)

/*
 * LOCAL: [0, ALLOC_LO) = EC + SGI/PPI (structural; no reserve).
 * GLOBAL: one entry per global_irq_id; length from @c gic_v2_nr_spi.
 * global_irq_id is SPI index = INTID - SPI_START.
 * local_irq_id = INTID + AARCH64_IRQ_OFFSET (offset 64).
 *
 * Each CPU has the same local_irq_id number line. Low ids live in
 * local_irq_vector[]; high ids are virtual (not in that vector) and map 1:1
 * into the shared global_irq_vector[] (formula, no per-CPU map).
 *
 *   CPU0 local_irq_vector[] (real, private)     virtual ids (not in local)
 *   +-----+-----+-----+-----+                   +-----+-------+-----+
 *   |  0  |  1  | ... |LO-1 |                   | LO  | LO+1  | ... |
 *   +-----+-----+-----+-----+                   +--+--+--+----+-----+
 *                                                  |     |
 *                                                  |     |
 *                                                  v     v
 *   CPU1 local_irq_vector[] (real, private)     +-----+-------+-----+
 *   +-----+-----+-----+-----+                   | LO  | LO+1  | ... |
 *   |  0  |  1  | ... |LO-1 |                   +--+--+--+----+-----+
 *   +-----+-----+-----+-----+                      |     |
 *                                                  |     |
 *                                                  v     v
 *                    shared global_irq_vector[] (real, machine-wide)
 *                    +------+------+------+-----+
 *                    | g[0] | g[1] | g[2] | ... |
 *                    +--+---+--+---+------+-----+
 *                       ^      ^
 *                       |      |
 *              all CPUs LO     all CPUs LO+1     (1:1 formula)
 *
 * LO = ARCH_IRQ_ID_ALLOC_LO; g[i] = global_irq_vector[i] (SPI index i).
 */
#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO
#define NR_IRQ            NR_ARCH_LOCAL_IRQ /* compat */

/**
 * Arch global external irq entry. Index is @c global_irq_id (SPI index).
 * Direct map: global_irq_id = SPI index, local_irq_id = INTID + offset.
 */
struct arch_global_irq {
        GLOBAL_IRQ_COMMON
};

/*
 * local_irq_id → global_irq_id by formula (no per-CPU map table).
 * @c cpu_id is recorded at alloc for later GIC routing.
 * @c arch_global_bind_local_irq_id is a no-op (always succeeds if cpu is
 * valid).
 * @c arch_choose_local_irq_id returns the formula local_irq_id.
 * Common APIs: @c rendezvos/trap/irq_space.h.
 */

#endif /* _RENDEZVOS_AARCH64_IRQ_SPACE_H_ */
