#ifndef _RENDEZVOS_GIC_H_
#define _RENDEZVOS_GIC_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/trap_common.h> /* enum irq_trigger */

/*
 * Common GIC surface for trap / boot (@c struct gic_chip gic).
 *
 * INTID ranges (SGI/PPI/SPI) live here. Version-specific MMIO and affinity
 * stay in the driver that @c gic_probe binds from DT compatible.
 *
 * Only GICv2 binds successfully today; @c arm,gic-v3 matches then fails
 * (@c -E_RENDEZVOS) — no silent fallback to v2.
 */

#define GIC_SGI_START 0
#define GIC_SGI_END   15
#define GIC_PPI_START 16
#define GIC_PPI_END   31
#define GIC_SPI_START 32
#define GIC_SPI_END   1019

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
#define GIC_DT_IRQ_EDGE_BOTH    (GIC_DT_IRQ_EDGE_RISING | GIC_DT_IRQ_EDGE_FALLING)
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

enum gic_version {
        GIC_VER_NONE = 0,
        GIC_VER_V2 = 2,
};

/**
 * Ack / EOI payload for @c gic.read_irq_num / @c gic.eoi.
 * GICv2 packing: INTID[9:0] + CPUID[12:10] from GICC_IAR/EOIR.
 */
union irq_source {
        u64 irq_source_value;
        struct {
                u64 irq_id : 10;
                u64 cpu_id : 3;
        } __attribute__((packed));
} __attribute__((packed));

struct gic_chip {
        enum gic_version version;

        void (*init_distributor)(void);
        void (*init_cpu_interface)(void);

        error_t (*enable_spi)(u32 intid, cpu_id_t cpu_id,
                              enum irq_trigger trigger);
        error_t (*disable_spi)(u32 intid);

        void (*set_priority)(u32 intid, u32 prio);

        void (*mask_irq)(u32 irq_num);
        void (*unmask_irq)(u32 irq_num);

        void (*send_sgi)(u32 irq_num, u32 target_mode, u32 target_list_bit);

        union irq_source (*read_irq_num)(void);
        void (*eoi)(union irq_source source);
        bool (*intid_is_pending)(u32 intid);
        void (*pending_clr)(u32 irq_number);
        u32 (*nr_spi)(void);
};

extern struct gic_chip gic;

/**
 * Walk DT compatible strings, bind the first supported GIC driver into @c gic.
 *
 * @return @c REND_SUCCESS, @c -E_REND_NOFOUND if no node, @c -E_RENDEZVOS if
 *         matched but unsupported (e.g. GICv3) or bind failed.
 */
error_t gic_probe(void);

static inline u32 gic_spi_intid(u32 spi_index)
{
        return (u32)GIC_SPI_START + spi_index;
}

static inline bool gic_is_sgi(u32 irq_num)
{
        return irq_num <= (u32)GIC_SGI_END;
}

static inline bool gic_is_ppi(u32 irq_num)
{
        return irq_num >= (u32)GIC_PPI_START && irq_num <= (u32)GIC_PPI_END;
}

static inline bool gic_is_spi(u32 irq_num)
{
        return irq_num >= (u32)GIC_SPI_START && irq_num <= (u32)GIC_SPI_END;
}

#endif /* _RENDEZVOS_GIC_H_ */
