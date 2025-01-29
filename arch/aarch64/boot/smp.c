#include <arch/aarch64/smp.h>
#include <arch/aarch64/mm/pmm.h>
extern char ap_entry;
extern int BSP_ID;
extern struct device_node* device_root;
void arch_start_smp(struct setup_info* arch_setup_info)
{
        struct device_node* cpu_node = dev_node_find_by_type(NULL, "cpu");
        while (cpu_node) {
                struct property* reg_prop =
                        dev_node_find_property(cpu_node, "reg", 4);
                if (!reg_prop) {
                        pr_error(
                                "[ SMP ] cannot find reg property in cpu node\n");
                        continue;
                }
                struct property* method_prop =
                        dev_node_find_property(cpu_node, "enable-method", 14);
                char* method_str;
                property_read_string(method_prop, &method_str);
                if (strcmp_s(method_str, "psci", 5)) {
                        pr_error(
                                "[ SMP ] we only support psci smp setup method now\n");
                        continue;
                }
                u32 reg_val;
                int err = property_read_u32(reg_prop, &reg_val);
                if (!err) {
                        pr_info("cpu node is %d\n", reg_val);
                        if (reg_val == BSP_ID)
                                goto next_cpu_node;

                        i32 res = psci_func.cpu_on(
                                reg_val,
                                KERNEL_VIRT_TO_PHY((vaddr)&ap_entry),
                                reg_val);
                        if (res != psci_succ) {
                                pr_error("[ SMP ] psci start smp fail\n");
                        }
                }
        next_cpu_node:
                cpu_node = dev_node_find_by_type(dev_tree_get_next(cpu_node),
                                                 "cpu");
        }
}