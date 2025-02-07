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
        for (paddr base = gicd_base_addr; base < gicd_base_addr + gicd_len;
             base += PAGE_SIZE) {
                map(&vspace_root,
                    PPN(base),
                    VPN(KERNEL_PHY_TO_VIRT(base)),
                    3,
                    PAGE_ENTRY_UNCACHED,
                    &per_cpu(Map_Handler, BSP_ID));
        }
        for (paddr base = gicc_base_addr; base < gicc_base_addr + gicc_len;
             base += PAGE_SIZE) {
                map(&vspace_root,
                    PPN(base),
                    VPN(KERNEL_PHY_TO_VIRT(base)),
                    3,
                    PAGE_ENTRY_UNCACHED,
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
void gic_v2_init_distributor(void)
{
        dsb(SY);
        gic.gicd->GICD_CTLR = GIC_GICD_CTLR_GROUP0_ENABLE
                              | GIC_GICD_CTLR_GROUP1_ENABLE;
        dsb(SY);
}
void gic_v2_init_cpu_interface(void)
{
        gic.gicc->GICC_CTLR = 1;
        gic.gicc->GICC_PMR = 0xff; /*support all the interrupts for this cpu*/
        dsb(NSH);
}
void gicd_v2_unmask_irq(u32 irq_num)
{
        /*write the GICD_ISENABLER reg to unmask*/
}
void gicd_v2_mask_irq(u32 irq_num)
{
        /*write the GICD_ICENABLER reg to mask*/
}
void gicd_v2_set_type(u32 irq_num, u32 type)
{
        /*write the GICD_ICFGR reg*/
}
void gicd_v2_set_priority(u32 irq_num, u32 prio)
{
        /*write the GICD_IPRIORITYR reg*/
}
void gicd_v2_set_affinity(u32 irq_num, u32 cpu_id)
{
        /*write the GICD_ITARGETSR reg*/
}
void gicd_v2_send_sgi()
{
        /*write the GICD_SGIR reg*/
}
void gicc_v2_eoi(u32 irq_num)
{
        /*
            here we should set the EOI mode and then just use GICC_EOIR reg
            without the GICC_DIR reg
        */
}
u32 gicc_v2_read_irq()
{
        /*read the GICC_IAR reg*/
}

struct gic_v2 gic = {
        .compatible = "arm,cortex-a15-gic",
        .probe = gic_v2_probe,
        .init_distributor = gic_v2_init_distributor,
        .init_cpu_interface = gic_v2_init_cpu_interface,
};