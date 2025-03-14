#include <arch/aarch64/gic/gic_v2.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/map_handler.h>
#include <modules/dtb/dtb.h>
#include <shampoos/percpu.h>
#include <arch/aarch64/sync/barrier.h>
extern int BSP_ID;

void map_gic_mem(u64 gicd_base_addr, u64 gicd_len, u64 gicc_base_addr,
                 u64 gicc_len)
{
        paddr vspace_root = get_current_kernel_vspace_root();
        /*here we set it as device memory to avoid dsb and dmb*/
        for (paddr base = gicd_base_addr; base < gicd_base_addr + gicd_len;
             base += PAGE_SIZE) {
                map(&vspace_root,
                    PPN(base),
                    VPN(KERNEL_PHY_TO_VIRT(base)),
                    3,
                    PAGE_ENTRY_DEVICE,
                    &per_cpu(Map_Handler, BSP_ID));
        }
        for (paddr base = gicc_base_addr; base < gicc_base_addr + gicc_len;
             base += PAGE_SIZE) {
                map(&vspace_root,
                    PPN(base),
                    VPN(KERNEL_PHY_TO_VIRT(base)),
                    3,
                    PAGE_ENTRY_DEVICE,
                    &per_cpu(Map_Handler, BSP_ID));
        }
}
void gic_v2_probe()
{
        char* reg_property = "reg";
        struct device_node* gic_node =
                dev_node_find_by_compatible(NULL, gic.compatible);
        if (!gic_node) {
                pr_error("[ GIC ] wrong gic node\n");
                return;
        }
        struct property* reg_prop =
                dev_node_find_property(gic_node, reg_property, 4);
        if (!reg_prop) {
                pr_error("[ GIC ] wrong gic node property\n");
                return;
        }
        u64 gic_regs[4];
        property_read_u64_arr(reg_prop, gic_regs, 4);
        map_gic_mem(gic_regs[0], gic_regs[1], gic_regs[2], gic_regs[3]);
        gic.gicd = (struct gic_distributor*)KERNEL_PHY_TO_VIRT(gic_regs[0]);
        gic.gicc = (struct gic_cpu_interface*)KERNEL_PHY_TO_VIRT(gic_regs[2]);
}
void gicd_v2_unmask_irq(u32 irq_num)
{
        /*write the GICD_ISENABLER reg to unmask*/
        gic.gicd->GICD_ISENABLERn[irq_num / 32] |= 1 << (irq_num % 32);
}
void gicd_v2_mask_irq(u32 irq_num)
{
        /*write the GICD_ICENABLER reg to mask*/
        gic.gicd->GICD_ICENABLERn[irq_num / 32] |= 1 << (irq_num % 32);
        isb();
}
void gicd_v2_set_type(u32 irq_num, u32 type)
{
        /*write the GICD_ICFGR reg*/
        gic.gicd->GICD_ICFGRn[irq_num / 16] |= type << ((irq_num % 16) * 2);
}
void gicd_v2_set_priority(u32 irq_num, u32 prio)
{
        /*write the GICD_IPRIORITYR reg*/
        gic.gicd->GICD_IPRIORITYRn[irq_num / 4] |=
                (prio & GIC_V2_IPRIORITYR_MASK) << ((irq_num % 4) * 8);
}
void gicd_v2_set_affinity(u32 irq_num, u32 cpu_id_mask)
{
        /*write the GICD_ITARGETSR reg*/
        if (!gic_v2_is_spi(irq_num) || cpu_id_mask >= GIC_V2_ITARGETSR_MASK)
                return; /*must >= 32 as spi*/
        u32 spi_irq_num = irq_num - GIC_V2_SPI_START;
        gic.gicd->GICD_ITARGETSRn_RW[irq_num / 4] |=
                (cpu_id_mask & GIC_V2_ITARGETSR_MASK)
                << ((spi_irq_num % 4) * 8);
}
void gicd_v2_send_sgi(u32 irq_num, u32 target_mode, u32 target_list_bit)
{
        /*write the GICD_SGIR reg*/
        if (!gic_v2_is_sgi(irq_num))
                return;
        switch (target_mode & GIC_V2_GICD_SGIR_TARGET_LIST_FLITER_MASK) {
        case GIC_V2_GICD_SGIR_TARGET_SPECIFIED:
                gic.gicd->GICD_SGIR =
                        GIC_V2_GICD_SGIR_TARGET_SPECIFIED | irq_num
                        | (target_list_bit & GIC_V2_GICD_SGIR_TARGET_LIST_MASK);
                break;
        case GIC_V2_GICD_SGIR_TARGET_OTHER:
                gic.gicd->GICD_SGIR = GIC_V2_GICD_SGIR_TARGET_OTHER | irq_num;
                break;
        case GIC_V2_GICD_SGIR_TARGET_SELF:
                gic.gicd->GICD_SGIR = GIC_V2_GICD_SGIR_TARGET_SELF | irq_num;
                break;
        default:
                break;
        }
        isb();
}
void gicc_v2_eoi(struct irq_source source)
{
        /*
            here we should set the EOI mode and then just use GICC_EOIR reg
            without the GICC_DIR reg
        */
        u32 eoir_value = ((source.cpu_id << GIC_V2_GICC_EOIR_CPU_ID_SHIFT)
                          & GIC_V2_GICC_EOIR_CPU_ID_MASK)
                         + (source.irq_id & GIC_V2_GICC_EOIR_INT_ID_MASK);
        gic.gicc->GICC_EOIR = eoir_value;
        isb();
}
struct irq_source gicc_v2_read_irq()
{
        /*read the GICC_IAR reg*/
        u32 gicc_iar_value = gic.gicc->GICC_IAR;
        struct irq_source source = {
                .cpu_id = (gicc_iar_value & GIC_V2_GICC_IAR_CPU_ID_MASK)
                          >> GIC_V2_GICC_IAR_CPU_ID_SHIFT,
                .irq_id = gicc_iar_value & GIC_V2_GICC_IAR_INT_ID_MASK,
        };
        isb();
        return source;
}
void gic_v2_init_distributor(void)
{
        /*disable gicd first*/
        gic.gicd->GICD_CTLR = 0;

        /*read the gicd type reg*/
        u32 gicd_typer_value = gic.gicd->GICD_TYPER;
        u32 nr_it_lines =
                (gicd_typer_value & GIC_V2_GICD_TYPER_IT_LINE_MASK) + 1;
        u32 irq_num = nr_it_lines << 5;
        if (irq_num > GIC_V2_SPI_END)
                irq_num = GIC_V2_SPI_END;
        u32 cpu_num = (gicd_typer_value & GIC_V2_GICD_TYPER_CPU_NUM_MASK)
                      >> GIC_V2_GICD_TYPER_CPU_NUM_SHIFT;
        pr_info("[ GIC ] irq num %d and cpu num %d\n", irq_num, cpu_num);

        /*set all the irq type , */
        for (u32 irq = GIC_V2_SPI_START; irq < irq_num; irq++) {
                gic.set_type(irq, GIC_V2_GICD_EDGE_TRIGGER|GIC_V2_GICD_1_N);
        }
        /*set all the irq send to core 0*/
        for (u32 irq = GIC_V2_SPI_START; irq < irq_num; irq++) {
                gic.set_affinity(irq, 0);
        }
        /*set priority*/
        for (u32 irq = GIC_V2_SPI_START; irq < irq_num; irq++) {
                gic.set_priority(irq, irq / 8);
        }
        /*disable all irqs*/
        for (u32 irq = GIC_V2_SPI_START; irq < irq_num; irq++) {
                gic.mask_irq(irq);
        }
        /*enable gicd*/
        gic.gicd->GICD_CTLR = GIC_V2_GICD_CTLR_GROUP0_ENABLE
                              | GIC_V2_GICD_CTLR_GROUP1_ENABLE;
        isb();
}
void gic_v2_init_cpu_interface(void)
{
        /*set the sgi priority*/
        for (u32 irq = GIC_V2_SGI_START; irq < GIC_V2_SGI_END; irq++) {
                gic.set_priority(irq, irq / 8);
        }
        /*set the ppi priority*/
        for (u32 irq = GIC_V2_PPI_START; irq < GIC_V2_PPI_END; irq++) {
                gic.set_priority(irq, irq / 8);
        }
        /*enable ppi and sgi*/
        for (u32 irq = GIC_V2_SGI_START; irq < GIC_V2_SGI_END; irq++) {
                gic.unmask_irq(irq);
        }
        for (u32 irq = GIC_V2_PPI_START; irq < GIC_V2_PPI_END; irq++) {
                gic.unmask_irq(irq);
        }

        gic.gicc->GICC_BPR = 0x3; /*we set 8 irq as the same priority group*/
        gic.gicc->GICC_PMR = 0xff; /*support all the interrupts for this cpu*/
        gic.gicc->GICC_CTLR = GIC_V2_GICC_CTLR_ENABLE_GROUP1;
        isb();
}

struct gic_v2 gic = {.compatible = "arm,cortex-a15-gic",
                     .probe = gic_v2_probe,
                     .init_distributor = gic_v2_init_distributor,
                     .init_cpu_interface = gic_v2_init_cpu_interface,
                     .unmask_irq = gicd_v2_unmask_irq,
                     .mask_irq = gicd_v2_mask_irq,
                     .set_type = gicd_v2_set_type,
                     .set_priority = gicd_v2_set_priority,
                     .set_affinity = gicd_v2_set_affinity,
                     .send_sgi = gicd_v2_send_sgi,
                     .read_irq_num = gicc_v2_read_irq,
                     .eoi = gicc_v2_eoi};