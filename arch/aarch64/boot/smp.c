#include <arch/aarch64/smp.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/cpuinfo.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>
extern char ap_entry;
extern int BSP_ID;
extern int NR_CPU;
extern enum cpu_status CPU_STATE;
extern struct cpuinfo cpu_info;
extern struct nexus_node* nexus_root;
DEFINE_PER_CPU(struct device_node*, cpu_device_node);
void arch_start_smp(struct setup_info* arch_setup_info)
{
        NR_CPU = 1;
        per_cpu(CPU_STATE, BSP_ID) = cpu_enable;
#ifdef NR_CPUS
        if (NR_CPUS == 1)
                return;
#endif
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
                        if (reg_val == BSP_ID)
                                goto next_cpu_node;
                        vaddr stack_bottom =
                                (vaddr)get_free_page(2,
                                                     ZONE_NORMAL,
                                                     KERNEL_VIRT_OFFSET,
                                                     0,
                                                     per_cpu(nexus_root,
                                                             BSP_ID))
                                + 2 * PAGE_SIZE;
                        arch_setup_info->ap_boot_stack_ptr = stack_bottom;
                        per_cpu(CPU_STATE, NR_CPU) = cpu_disable;
                        i32 res = psci_func.cpu_on(
                                reg_val,
                                KERNEL_VIRT_TO_PHY((vaddr)&ap_entry),
                                NR_CPU);
                        if (res != psci_succ) {
                                pr_error("[ SMP ] psci start ap %d fail\n",
                                         reg_val);
                                NR_CPU++;
                                continue;
                        }
                        per_cpu(cpu_device_node, NR_CPU) = cpu_node;
                        /*then wait for the target cpu set it self enable*/
                        while (per_cpu(CPU_STATE, NR_CPU) == cpu_disable)
                                printk("", LOG_DEBUG);
                        NR_CPU++;
                }
        next_cpu_node:
                cpu_node = dev_node_find_by_type(dev_tree_get_next(cpu_node),
                                                 "cpu");
        }
}