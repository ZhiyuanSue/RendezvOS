#include <arch/aarch64/smp.h>
#include <arch/aarch64/mm/pmm.h>
extern char ap_entry;
void arch_start_smp(struct setup_info *arch_setup_info)
{
        i32 res = psci_func.cpu_on(1, KERNEL_VIRT_TO_PHY((vaddr)&ap_entry), 1);
        if (res != psci_succ) {
                pr_error("[ SMP ] psci start smp fail\n");
        }
}