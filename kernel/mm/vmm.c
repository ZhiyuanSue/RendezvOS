#include <shampoos/mm/vmm.h>

error_t map(paddr vspace_root_paddr, u64 ppn, u64 vpn, int level)
{
        vaddr vspace_root_vaddr = KERNEL_PHY_TO_VIRT(vspace_root_paddr);
        union L0_entry *L0_table = (union L0_entry *)vspace_root_vaddr;
}