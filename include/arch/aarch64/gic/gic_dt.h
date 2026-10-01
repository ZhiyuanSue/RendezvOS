#ifndef _RENDEZVOS_GIC_DT_H_
#define _RENDEZVOS_GIC_DT_H_

#include <common/types.h>

/*
 * GIC Device Tree interrupt specifier (#interrupt-cells = 3):
 *   <type, irq, flags>
 *
 * Shared by GICv2 and GICv3 DT bindings.
 *
 * type — INTID space:
 *   SPI → INTID = 32 + irq
 *   PPI → INTID = 16 + irq
 * irq  — index within that space (not the final INTID).
 * flags — packed field below.
 */

#define GIC_DT_TYPE_SPI 0u
#define GIC_DT_TYPE_PPI 1u

/*
 * flags layout (third cell):
 *
 *   bits[3:0]   trigger / polarity (same encoding as Linux IRQ_TYPE_*)
 *   bits[7:4]   reserved
 *   bits[15:8]  PPI CPU affinity mask (GICv1/v2 only; ignored on GICv3)
 *   bits[31:16] reserved
 */

/* bits[3:0]: may OR together (e.g. EDGE_BOTH = RISING | FALLING). */
#define GIC_DT_IRQ_EDGE_RISING  0x1u
#define GIC_DT_IRQ_EDGE_FALLING 0x2u
#define GIC_DT_IRQ_EDGE_BOTH \
        (GIC_DT_IRQ_EDGE_RISING | GIC_DT_IRQ_EDGE_FALLING)
#define GIC_DT_IRQ_LEVEL_HIGH   0x4u
#define GIC_DT_IRQ_LEVEL_LOW    0x8u
#define GIC_DT_IRQ_TRIGGER_MASK 0xfu

/*
 * bits[15:8]: which CPUs may take this PPI (GICv1/v2).
 * Bit N set ⇒ logical CPU N. Encode with GIC_DT_PPI_CPU_MASK(cpu_bitset).
 * GICv3 PPIs are per-PE; firmware leaves this field 0 / unused.
 */
#define GIC_DT_PPI_CPU_MASK_SHIFT 8u
#define GIC_DT_PPI_CPU_MASK_MASK  (0xffu << GIC_DT_PPI_CPU_MASK_SHIFT)
#define GIC_DT_PPI_CPU_MASK(cpus) \
        (((u32)(cpus) << GIC_DT_PPI_CPU_MASK_SHIFT) & GIC_DT_PPI_CPU_MASK_MASK)

#endif
