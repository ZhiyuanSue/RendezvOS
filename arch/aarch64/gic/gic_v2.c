#include <arch/aarch64/gic/gic_v2.h>
#include <arch/aarch64/sync/barrier.h>
#include <common/bit.h>
#include <common/stddef.h>
#include <modules/dtb/dtb.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/smp/percpu.h>

/* Private GICv2 MMIO; public surface is @c struct gic_chip gic. */
static struct gic_distributor *gicd_v2;
static struct gic_cpu_interface *gicc_v2;

static void map_gic_range(u64 phy_base, u64 len)
{
        for (paddr base = phy_base; base < phy_base + len; base += PAGE_SIZE) {
                map(per_cpu(current_vspace, BSP_ID),
                    PPN(base),
                    VPN(KERNEL_PHY_TO_VIRT(base)),
                    3,
                    PAGE_ENTRY_DEVICE | PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                            | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                    &per_cpu(Map_Handler, BSP_ID));
        }
}

static void gicd_v2_unmask_irq(u32 irq_num)
{
        gicd_v2->GICD_ISENABLERn[irq_num / 32] |= 1u << (irq_num % 32);
}

static void gicd_v2_mask_irq(u32 irq_num)
{
        gicd_v2->GICD_ICENABLERn[irq_num / 32] |= 1u << (irq_num % 32);
        isb();
}

static void gicd_v2_set_type(u32 irq_num, u32 type)
{
        u32 idx = irq_num / 16;
        u32 shift = (irq_num % 16) * 2;
        u32 field_mask = 0x3u << shift;
        u32 val = gicd_v2->GICD_ICFGRn[idx];

        val = clear_mask_u32(val, field_mask);
        val = set_mask_u32(val, (type & 0x3u) << shift);
        gicd_v2->GICD_ICFGRn[idx] = val;
        isb();
}

static void gicd_v2_set_priority(u32 irq_num, u32 prio)
{
        gicd_v2->GICD_IPRIORITYRn[irq_num / 4] |=
                (prio & GIC_V2_IPRIORITYR_MASK) << ((irq_num % 4) * 8);
}

static error_t gicd_v2_set_cpu_affinity(u32 irq_num, cpu_id_t cpu_id)
{
        if (!gic_is_spi(irq_num))
                return -E_IN_PARAM;
        if (cpu_id
            >= (cpu_id_t)MIN(RENDEZVOS_MAX_CPU_NUMBER, GIC_V2_NR_CPU_MAX))
                return -E_IN_PARAM;

        u32 mask = 1u << (u32)cpu_id;
        u32 spi = irq_num - GIC_SPI_START;
        u32 shift = (spi % 4) * 8;
        u32 val = gicd_v2->GICD_ITARGETSRn_RW[spi / 4];
        val = clear_mask_u32(val, (u32)GIC_V2_ITARGETSR_MASK << shift);
        val = set_mask_u32(val, mask << shift);
        gicd_v2->GICD_ITARGETSRn_RW[spi / 4] = val;
        isb();
        return REND_SUCCESS;
}

static void gicd_v2_send_sgi(u32 irq_num, u32 target_mode, u32 target_list_bit)
{
        if (!gic_is_sgi(irq_num))
                return;

        switch (target_mode & GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_MASK) {
        case GIC_V2_GICD_SGIR_TARGET_SPECIFIED:
                gicd_v2->GICD_SGIR =
                        GIC_V2_GICD_SGIR_TARGET_SPECIFIED | irq_num
                        | (target_list_bit & GIC_V2_GICD_SGIR_TARGET_LIST_MASK);
                break;
        case GIC_V2_GICD_SGIR_TARGET_OTHER:
                gicd_v2->GICD_SGIR = GIC_V2_GICD_SGIR_TARGET_OTHER | irq_num;
                break;
        case GIC_V2_GICD_SGIR_TARGET_SELF:
                gicd_v2->GICD_SGIR = GIC_V2_GICD_SGIR_TARGET_SELF | irq_num;
                break;
        default:
                break;
        }
        isb();
}

static void gicc_v2_eoi(union irq_source source)
{
        /*
            here we should set the EOI mode and then just use GICC_EOIR reg
            without the GICC_DIR reg
        */
        gicc_v2->GICC_EOIR = (u32)source.irq_source_value;
        isb();
}

static union irq_source gicc_v2_read_irq(void)
{
        /*read the GICC_IAR reg*/
        union irq_source source = {
                .irq_source_value = gicc_v2->GICC_IAR,
        };
        isb();
        return source;
}

static error_t gic_v2_enable_spi(u32 intid, cpu_id_t cpu_id,
                                 enum irq_trigger trigger_mode)
{
        if (!gicd_v2)
                return -E_RENDEZVOS;
        if (!gic_is_spi(intid))
                return -E_IN_PARAM;

        /* Portable sense → GICD_ICFGR bit encoding (not the same 0/1). */
        u32 type = (trigger_mode == IRQ_TRIGGER_LEVEL) ?
                           GIC_V2_GICD_LEVEL_TRIGGER :
                           GIC_V2_GICD_EDGE_TRIGGER;
        gicd_v2_set_type(intid, type);
        error_t e = gicd_v2_set_cpu_affinity(intid, cpu_id);
        if (e != REND_SUCCESS)
                return e;
        gicd_v2_unmask_irq(intid);
        return REND_SUCCESS;
}

static error_t gic_v2_disable_spi(u32 intid)
{
        if (!gicd_v2)
                return -E_RENDEZVOS;
        if (!gic_is_spi(intid))
                return -E_IN_PARAM;
        gicd_v2_mask_irq(intid);
        return REND_SUCCESS;
}

static bool gic_v2_intid_is_pending(u32 intid)
{
        if (!gicd_v2)
                return false;
        if (intid > (u32)GIC_SPI_END)
                return false;

        u32 idx = intid / 32;
        u32 bit = 1u << (intid % 32);

        if (gicd_v2->GICD_ISACTIVERn[idx] & bit)
                return true;
        if ((gicd_v2->GICD_ISENABLERn[idx] & bit)
            && (gicd_v2->GICD_ISPENDRn[idx] & bit))
                return true;
        return false;
}

static void gicd_v2_pending_clr(u32 irq_num)
{
        gicd_v2->GICD_ICPENDRn[irq_num / 32] |= 1u << (irq_num % 32);
        isb();
}

static u32 gic_v2_nr_spi(void)
{
        return (u32)(GIC_SPI_END - GIC_SPI_START + 1);
}

static void gic_v2_init_distributor(void)
{
        /*read the gicd type reg*/
        u32 gicd_typer_value = gicd_v2->GICD_TYPER;
        u32 nr_it_lines =
                (gicd_typer_value & GIC_V2_GICD_TYPER_IT_LINE_MASK) + 1;
        u32 irq_num = nr_it_lines << 5;
        if (irq_num > GIC_SPI_END)
                irq_num = GIC_SPI_END;
        u32 cpu_num = (gicd_typer_value & GIC_V2_GICD_TYPER_CPU_NUM_MASK)
                      >> GIC_V2_GICD_TYPER_CPU_NUM_SHIFT;

        gicd_v2->GICD_CTLR = 0;
        print("[ GIC ] irq num %u cpu num %u\n", irq_num, cpu_num);

        for (u32 irq = GIC_SPI_START; irq < irq_num; irq++) {
                gicd_v2_set_type(irq,
                                 GIC_V2_GICD_EDGE_TRIGGER | GIC_V2_GICD_1_N);
                gic.set_priority(irq, irq / 8);
                gic.mask_irq(irq);
        }

        gicd_v2->GICD_CTLR = GIC_V2_GICD_CTLR_GROUP0_ENABLE;
        isb();
}

static void gic_v2_init_cpu_interface(void)
{
        for (u32 irq = GIC_SGI_START; irq <= GIC_PPI_END; irq++) {
                gic.set_priority(irq, irq / 8);
                gic.unmask_irq(irq);
                gicd_v2_set_type(irq,
                                 irq <= GIC_SGI_END ?
                                         GIC_V2_GICD_EDGE_TRIGGER :
                                         GIC_V2_GICD_LEVEL_TRIGGER);
        }

        gicc_v2->GICC_BPR = 0x3; /*we set 8 irq as the same priority group*/
        gicc_v2->GICC_PMR = 0xff; /*support all the interrupts for this cpu*/
        gicc_v2->GICC_CTLR = GIC_V2_GICC_CTLR_ENABLE_GROUP1;
        isb();
}

static const struct gic_chip gic_v2_chip = {
        .version = GIC_VER_V2,
        .init_distributor = gic_v2_init_distributor,
        .init_cpu_interface = gic_v2_init_cpu_interface,
        .enable_spi = gic_v2_enable_spi,
        .disable_spi = gic_v2_disable_spi,
        .set_priority = gicd_v2_set_priority,
        .mask_irq = gicd_v2_mask_irq,
        .unmask_irq = gicd_v2_unmask_irq,
        .send_sgi = gicd_v2_send_sgi,
        .read_irq_num = gicc_v2_read_irq,
        .eoi = gicc_v2_eoi,
        .intid_is_pending = gic_v2_intid_is_pending,
        .pending_clr = gicd_v2_pending_clr,
        .nr_spi = gic_v2_nr_spi,
};

error_t gic_v2_bind(struct device_node *node)
{
        struct property *reg_prop;
        u64 gic_regs[4];

        if (!node)
                return -E_IN_PARAM;

        reg_prop = dev_node_find_property(node, "reg", 4);
        if (!reg_prop) {
                print("[ GIC ] GICv2 node missing reg\n");
                return -E_RENDEZVOS;
        }

        property_read_u64_arr(reg_prop, gic_regs, 4);
        map_gic_range(gic_regs[0], gic_regs[1]);
        map_gic_range(gic_regs[2], gic_regs[3]);
        gicd_v2 = (struct gic_distributor *)KERNEL_PHY_TO_VIRT(gic_regs[0]);
        gicc_v2 = (struct gic_cpu_interface *)KERNEL_PHY_TO_VIRT(gic_regs[2]);

        gic = gic_v2_chip;
        return REND_SUCCESS;
}
