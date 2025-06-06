#ifndef _RENDEZVOS_GIC_V2_H_
#define _RENDEZVOS_GIC_V2_H_
#include <common/types.h>
#include <common/stdbool.h>
/*
here is some ref codes:
https://developer.aliyun.com/article/1532907
*/
#define GIC_V2_NR_CPU_MAX 8

#define GIC_V2_SGI_START 0
#define GIC_V2_SGI_END   15
#define GIC_V2_PPI_START 16
#define GIC_V2_PPI_END   31
#define GIC_V2_SPI_START 32
#define GIC_V2_SPI_END   1019

static inline bool gic_v2_is_sgi(u32 irq_num)
{
        return (irq_num >= GIC_V2_SGI_START && irq_num <= GIC_V2_SGI_END);
}
static inline bool gic_v2_is_ppi(u32 irq_num)
{
        return (irq_num >= GIC_V2_PPI_START && irq_num <= GIC_V2_PPI_END);
}
static inline bool gic_v2_is_spi(u32 irq_num)
{
        return (irq_num >= GIC_V2_SPI_START && irq_num <= GIC_V2_SPI_END);
}

struct gic_distributor {
#define GIC_V2_GICD_CTLR_GROUP0_ENABLE (0x1)
#define GIC_V2_GICD_CTLR_GROUP1_ENABLE (0x2)
        volatile u32 GICD_CTLR; /*RW	0x000*/
#define GIC_V2_GICD_TYPER_IT_LINE_MASK  (0x1f)
#define GIC_V2_GICD_TYPER_CPU_NUM_SHIFT (5)
#define GIC_V2_GICD_TYPER_CPU_NUM_MASK  (0x7 << GIC_V2_GICD_TYPER_CPU_NUM_SHIFT)
#define GIC_V2_GICD_TYPER_SECURE_EXT    (1 << 10)
#define GIC_V2_GICD_TYPER_LSPI_SHIFT    (11)
#define GIC_V2_GICD_TYPER_LSPI_MASK     (0x1f << GIC_V2_GICD_TYPER_LSPI_SHIFT)
        volatile u32 GICD_TYPER; /*RO	0x004*/
        volatile u32 GICD_IIDR; /*RO	0x008*/
        u32 Res_0[0x5]; /*		0x00C - 0x01C*/

        volatile u32 Impl_0[0x8]; /*		0x020 - 0x3C*/
        /*IMPLEMENTATION DEFINED registers*/

        u32 Res_1[0x10]; /*		0x040 - 0x7C*/
        volatile u32 GICD_IGROUPRn; /*RW	0x080*/
        volatile u32 ZERO_0[0x1F]; /*		0x084 - 0xFC*/
        volatile u32 GICD_ISENABLERn[0x20]; /*RW	0x100 - 0x17C*/
        volatile u32 GICD_ICENABLERn[0x20]; /*RW	0x180 - 0x1FC*/
        volatile u32 GICD_ISPENDRn[0x20]; /*RW	0x200 - 0x27C*/
        volatile u32 GICD_ICPENDRn[0x20]; /*RW	0x280 - 0x2FC*/
        volatile u32 GICD_ISACTIVERn[0x20]; /*RW	0x300 - 0x37C*/
        volatile u32 GICD_ICACTIVERn[0x20]; /*RW	0x380 - 0x3FC*/
#define GIC_V2_IPRIORITYR_MASK (0xff)
        volatile u32 GICD_IPRIORITYRn[0xFF]; /*RW	0x400 - 0x7F8*/
        u32 Res_2; /*RW	0x7FC*/
#define GIC_V2_ITARGETSR_MASK (0xff)
        volatile u32 GICD_ITARGETSRn_RO[0x8]; /*RO	0x800 - 0x81C*/
        volatile u32 GICD_ITARGETSRn_RW[0xF7]; /*RW	0x820 - 0xBF8*/
        u32 Res_3; /*		0xBFC*/
#define GIC_V2_GICD_1_N          (1)
#define GIC_V2_GICD_EDGE_TRIGGER (1 << 1)
        volatile u32 GICD_ICFGRn[0x40]; /*RW	0xC00 - 0xCFC*/

        volatile u32 Impl_1[0x40]; /*		0xD00 - 0xDFC*/
        /*IMPLEMENTATION DEFINED registers*/

        volatile u32 GICD_NSACRn[0x40]; /*RW	0xE00 - 0xEFC*/
#define GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_SHIFT (24)
#define GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_MASK \
        (0x3 << GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_SHIFT)
#define GIC_V2_GICD_SGIR_TARGET_LIST_SHIFT (16)
#define GIC_V2_GICD_SGIR_TARGET_LIST_MASK \
        (0xff << GIC_V2_GICD_SGIR_TARGET_LIST_SHIFT)
#define GIC_V2_GICD_SGIR_TARGET_SPECIFIED (0)
#define GIC_V2_GICD_SGIR_TARGET_OTHER \
        (1 << GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_SHIFT)
#define GIC_V2_GICD_SGIR_TARGET_SELF \
        (2 << GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_SHIFT)
        volatile u32 GICD_SGIR; /*WO	0xF00*/
        u32 Res_4[0x3]; /*		0xF04 - 0xF0C*/
        volatile u32 GICD_CPENDSGIRn[0x4]; /*RW	0xF10 - 0xF1C*/
        volatile u32 GICD_SPENDSGIRn[0x4]; /*RW	0xF20 - 0xF2C*/
        u32 Res_5[0x28]; /*		0xF30 - 0xFCC*/

        volatile u32 Impl_2[0x6]; /*		0xFD0 - 0xFE4*/ /*IMPLEMENTATION
                                                           DEFINED registers*/
        volatile u32 ICPIDR2; /*		0xFE8*/
        volatile u32 Impl_3[0x5]; /*		0xFEC - 0xFFC*/ /*IMPLEMENTATION
                                                           DEFINED registers*/
} __attribute__((packed));

struct gic_cpu_interface {
        /*this is no secure extension*/
#define GIC_V2_GICC_CTLR_ENABLE_GROUP1         (0x1)
#define GIC_V2_GICC_CTLR_FIQ_BYPASS_DIS_GROUP1 (0x1 << 5)
#define GIC_V2_GICC_CTLR_IRQ_BYPASS_DIS_GROUP1 (0x1 << 6)
#define GIC_V2_GICC_CTLR_EOI_MODE_NON_SECURE   (0x1 << 9)
        volatile u32 GICC_CTLR; /*RW	0x0000*/
        volatile u32 GICC_PMR; /*RW	0x0004*/
        volatile u32 GICC_BPR; /*RW	0x0008*/
#define GIC_V2_GICC_IAR_INT_ID_MASK  (0x3ff)
#define GIC_V2_GICC_IAR_CPU_ID_SHIFT (10)
#define GIC_V2_GICC_IAR_CPU_ID_MASK  (0x7 << GIC_V2_GICC_IAR_CPU_ID_SHIFT)
        volatile u32 GICC_IAR; /*RO	0x000C	reset - 0x000003FF*/
#define GIC_V2_GICC_EOIR_INT_ID_MASK  (0x3ff)
#define GIC_V2_GICC_EOIR_CPU_ID_SHIFT (10)
#define GIC_V2_GICC_EOIR_CPU_ID_MASK  (0x7 << GIC_V2_GICC_EOIR_CPU_ID_SHIFT)
        volatile u32 GICC_EOIR; /*WO	0x0010*/
        volatile u32 GICC_RPR; /*RO	0x0014	reset - 0x000000FF*/
        volatile u32 GICC_HPPIR; /*RO	0x0018	reset - 0x000003FF*/
        volatile u32 GICC_ABPR; /*RW	0x001C*/
        volatile u32 GICC_AIAR; /*RO	0x0020	reset - 0x000003FF*/
        volatile u32 GICC_AEOIR; /*WO	0x0024*/
        volatile u32 GICC_AHPPIR; /*RO	0x0028	reset - 0x000003FF*/
        u32 Res_0[0x5]; /*		0x002C - 0x003C*/

        volatile u32 Impl_0[0x24];
        /*		0x0040-0x00CF*/ /*IMPLEMENTATION DEFINED registers*/

        volatile u32 GICC_APRn[0x4]; /*RW	0x00D0 - 0x00DC*/
        volatile u8 GICC_NSAPRn[0xD]; /*RW	0x00E0 - 0x00EC*/
        u8 Res_1[0xF]; /*	0x00ED - 0x00F8*/
        volatile u32 GICC_IIDR; /*RO	0x00FC*/
        u32 Res_2[0x3C0]; /*	0x100-0x1000*/
        volatile u32 GICC_DIR; /*WO	0x1000*/
} __attribute__((packed));

struct gic_virtual_interface {
        volatile u32 GICH_HCR; /*RW	0x00*/
        volatile u32 GICH_VTR; /*RO	0x04*/
        volatile u32 GICH_VMCR; /*RW	0x08*/
        u32 Res_0; /*		0x0C*/
        volatile u32 GICH_MISR; /*RO	0x10*/
        u32 Res_1[0x3]; /*		0x14-0x1C*/
        volatile u32 GICH_EISR0; /*RO	0x20*/
        volatile u32 GICH_EISR1; /*RO	0x24*/
        u32 Res_2[0x2]; /*		0x28-0x2C*/
        volatile u32 GICH_ELSR0; /*RO	0x30*/
        volatile u32 GICH_ELSR1; /*RO	0x34*/
        u32 Res_3[0x2E]; /*	0x38-0xEC*/
        volatile u32 GICH_APR; /*RW	0xF0*/
        u32 Res_4[0x3]; /*		0xF4-0xFC*/
        volatile u32 GICH_LR[0x40]; /*RW	0x100-0x1FC*/
} __attribute__((packed));

union irq_source {
        u64 irq_source_value;
        struct {
                u64 irq_id : 10;
                u64 cpu_id : 3;
        } __attribute__((packed));
} __attribute__((packed));

struct gic_v2 {
        struct gic_distributor* gicd;
        struct gic_cpu_interface* gicc;
        char* compatible;
        void (*probe)(void);
        void (*init_distributor)(void);
        void (*init_cpu_interface)(void);
        void (*unmask_irq)(u32 irq_num);
        void (*mask_irq)(u32 irq_num);
        void (*set_type)(u32 irq_num, u32 type);
        void (*set_priority)(u32 irq_num, u32 prio);
        void (*set_affinity)(u32 irq_num, u32 cpu_id_mask);
        void (*send_sgi)(u32 irq_num, u32 target_mode, u32 target_list_bit);
        union irq_source (*read_irq_num)(void);
        void (*eoi)(union irq_source source);
        void (*pending_clr)(u32 irq_number);
};

extern struct gic_v2 gic;

#endif