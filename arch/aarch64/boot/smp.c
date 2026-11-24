#include <arch/aarch64/smp.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/cpuinfo.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/smp/cpu_topology.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/limits.h>
extern char ap_entry;
extern int NR_CPU;
DEFINE_PER_CPU(struct device_node*, cpu_device_node);
/*
 * @brief start smp cores, we get the smp info from dtb tree, and reuse the arch
 * setup info structure, then using psci start other cores one by one
 * @param arch_setup_info the aarch64 setup info, which is vaddr pointer
 */
void arch_start_smp(struct setup_info* arch_setup_info)
{
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++)
                per_cpu(CPU_STATE, i) = no_cpu;
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
                        goto next_cpu_node;
                }
                struct property* method_prop =
                        dev_node_find_property(cpu_node, "enable-method", 14);
                char* method_str;
                if (!method_prop) {
                        pr_error("[ SMP ] cpu node missing enable-method\n");
                        goto next_cpu_node;
                }
                property_read_string(method_prop, &method_str);
                if (strcmp_s(method_str, "psci", 5)) {
                        pr_error(
                                "[ SMP ] we only support psci smp setup method now\n");
                        goto next_cpu_node;
                }

                u32 reg_val;
                if (property_read_u32(reg_prop, &reg_val))
                        goto next_cpu_node;

                cpu_hwid_t hwid = (cpu_hwid_t)reg_val;
                if (hwid == cpu_hw_id(BSP_ID)) {
                        per_cpu(cpu_device_node, BSP_ID) = cpu_node;
                        goto next_cpu_node;
                }
                if (NR_CPU >= RENDEZVOS_MAX_CPU_NUMBER)
                        goto next_cpu_node;
#ifdef NR_CPUS
                if (NR_CPU >= NR_CPUS)
                        goto next_cpu_node;
#endif
                set_cpu_hw_id((cpu_id_t)NR_CPU, hwid);
                per_cpu(CPU_STATE, NR_CPU) = cpu_disable;
                per_cpu(cpu_device_node, NR_CPU) = cpu_node;
                NR_CPU++;
        next_cpu_node:
                cpu_node = dev_node_find_by_type(dev_tree_get_next(cpu_node),
                                                 "cpu");
        }

        if (NR_CPU <= 1)
                return;

        arch_irq_local_disable();
        for (int cpu = 0; cpu < NR_CPU; cpu++) {
                if ((cpu_id_t)cpu == BSP_ID)
                        continue;
                if (per_cpu(CPU_STATE, cpu) != cpu_disable)
                        continue;

                cpu_hwid_t hwid = cpu_hw_id((cpu_id_t)cpu);
                struct allocator* cpu_kallocator = per_cpu(kallocator, BSP_ID);
                void* ap_stack = NULL;
                if (cpu_kallocator)
                        ap_stack = cpu_kallocator->m_alloc(
                                cpu_kallocator, (size_t)16 * PAGE_SIZE);
                if (!ap_stack) {
                        pr_error(
                                "[ SMP ] AP stack kalloc fail logical %d reg %llu\n",
                                cpu,
                                (unsigned long long)hwid);
                        continue;
                }
                vaddr stack_bottom = (vaddr)ap_stack + 16 * PAGE_SIZE;
                arch_setup_info->ap_boot_stack_ptr = stack_bottom;
                arch_setup_info->cpu_id = (cpu_id_t)cpu;

                i32 res = (i32)psci_func.cpu_on(
                        hwid, KERNEL_VIRT_TO_PHY((vaddr)&ap_entry), (u64)cpu);
                if (res != psci_succ) {
                        pr_error(
                                "[ SMP ] psci start ap logical %d reg %llu fail\n",
                                cpu,
                                (unsigned long long)hwid);
                        continue;
                }

                while (per_cpu(CPU_STATE, cpu) == cpu_disable)
                        arch_cpu_relax();
        }
        arch_irq_local_enable();
}