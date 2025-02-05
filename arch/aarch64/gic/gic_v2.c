#include <arch/aarch64/gic/gic_v2.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/map_handler.h>
#include <modules/dtb/dtb.h>

void map_gic_mem(u64 gicd_base_addr, u64 gicd_len, u64 gicc_base_addr,
                 u64 gicc_len)
{
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
}
void gic_v2_init_distributor(void)
{
}
void gic_v2_init_cpu_interface(void)
{
}
void gic_v2_mask_irq()
{
}
void gic_v2_unmask_irq()
{
}

struct gic_v2 gic = {
        .compatible = "arm,cortex-a15-gic",
        .probe = gic_v2_probe,
        .init_distributor = gic_v2_init_distributor,
        .init_cpu_interface = gic_v2_init_cpu_interface,
};