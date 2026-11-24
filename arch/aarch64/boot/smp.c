#include <arch/aarch64/smp.h>
#include <arch/aarch64/mm/pmm.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/smp/cpu_topology.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/time.h>
#include <rendezvos/limits.h>
extern char ap_entry;
extern int NR_CPU;
/*
 * @brief start smp cores, we get the smp info from dtb tree, and reuse the arch
 * setup info structure, then using psci start other cores one by one
 * @param arch_setup_info the aarch64 setup info, which is vaddr pointer
 */
void arch_start_smp(struct setup_info* arch_setup_info)
{
        per_cpu(CPU_STATE, BSP_ID) = cpu_enable;
#ifdef NR_CPUS
        if (NR_CPUS == 1)
                return;
#endif
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

                for (int j = 0;
                     j < 1000 && per_cpu(CPU_STATE, cpu) == cpu_disable;
                     j++)
                        mdelay(10);
                if (per_cpu(CPU_STATE, cpu) == cpu_disable) {
                        pr_error(
                                "[ ERROR ]cpu %d (MPIDR %llu) cannot enable after 10s\n",
                                cpu,
                                (unsigned long long)hwid);
                }
        }
        arch_irq_local_enable();
}