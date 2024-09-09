#include <shampoos/mm/vmm.h>
#include <shampoos/error.h>
#include <modules/log/log.h>
error_t map(paddr vspace_root_paddr, u64 ppn, u64 vpn, int level)
{
        if (ROUND_DOWN(vspace_root_paddr, PAGE_SIZE) != vspace_root_paddr) {
                pr_error(
                        "[ ERROR ]wrong vspace root paddr in mapping, please check\n");
                return -EINVAL;
        }
        vaddr vspace_root_vaddr = KERNEL_PHY_TO_VIRT(vspace_root_paddr);
        union L0_entry *L0_table = (union L0_entry *)vspace_root_vaddr;
}