#ifndef _RENDEZVOS_AARCH64_IRQ_SPACE_H_
#define _RENDEZVOS_AARCH64_IRQ_SPACE_H_
#include <common/bit.h>
#include <common/types.h>
#include <arch/aarch64/gic/gic.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/irq_space_common.h>
#include "trap_def.h"

#define AARCH64_IRQ_TO_TRAP_ID(irq_number) (irq_number + AARCH64_IRQ_OFFSET)
#define AARCH64_TRAP_ID_TO_IRQ(trap_id)    (trap_id - AARCH64_IRQ_OFFSET)

/*
 * Arch piece of irq_space: software ids vs GIC INTID (macros above).
 *
 * Today's SPI space: global_irq_id == SPI index (base 0).
 */
#define ARCH_IRQ_INTID_IPI   GIC_SGI_START /* 0 */
#define ARCH_IRQ_ID_IPI      AARCH64_IRQ_TO_TRAP_ID(ARCH_IRQ_INTID_IPI)
#define ARCH_IRQ_ID_ALLOC_LO AARCH64_IRQ_TO_TRAP_ID(GIC_SPI_START)
#define ARCH_IRQ_ID_ALLOC_HI AARCH64_IRQ_TO_TRAP_ID(GIC_SPI_END)

/*
 * LOCAL: [0, ALLOC_LO) = EC + SGI/PPI (structural).
 * GLOBAL / delivery: SPI via space ops; local_irq_id = INTID + offset.
 */
#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO
#define NR_IRQ            NR_ARCH_LOCAL_IRQ /* compat */

/* No arch-only fields */
struct arch_global_irq {
        GLOBAL_IRQ_COMMON
};

struct arch_global_irq *arch_irq_entry_from_local(cpu_id_t cpu_id,
                                                  local_irq_id_t local_irq_id);

/* Register the GIC SPI irq_space */
error_t irq_space_gic_spi_register(size_t nr_spi);

#endif /* _RENDEZVOS_AARCH64_IRQ_SPACE_H_ */
