#include <shampoos/mm/vmm.h>
#include <common/string.h>
#include <shampoos/error.h>
#include <modules/log/log.h>
extern u64 *L0_table;
extern u64 *MAP_L1_table, *MAP_L2_table, *MAP_L3_table;
void init_map()
{
        ARCH_PFLAGS_t flags;
        paddr vspace_root = KERNEL_VIRT_TO_PHY((vaddr)&L0_table);
        // TODO:check the vspace root using get_current_kernel_vspace_root()
        flags = arch_decode_flags(0,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        arch_set_L0_entry(KERNEL_VIRT_TO_PHY((vaddr)&MAP_L1_table),
                          map_pages,
                          (union L0_entry *)KERNEL_PHY_TO_VIRT(vspace_root),
                          flags);
        flags = arch_decode_flags(1,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        arch_set_L1_entry(KERNEL_VIRT_TO_PHY((vaddr)&MAP_L2_table),
                          map_pages,
                          (union L1_entry *)&MAP_L1_table,
                          flags);
        flags = arch_decode_flags(2,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        arch_set_L2_entry(KERNEL_VIRT_TO_PHY((vaddr)&MAP_L3_table),
                          map_pages,
                          (union L2_entry *)&MAP_L2_table,
                          flags);
        // TODO:flush tlb
}
error_t map(paddr vspace_root_paddr, u64 ppn, u64 vpn, int level)
{
        if (ROUND_DOWN(vspace_root_paddr, PAGE_SIZE) != vspace_root_paddr) {
                pr_error(
                        "[ ERROR ]wrong vspace root paddr in mapping, please check\n");
                return -EINVAL;
        }
        vaddr vspace_root_vaddr = KERNEL_PHY_TO_VIRT(vspace_root_paddr);
        union L0_entry *L0_table = (union L0_entry *)vspace_root_vaddr;

        return 0;
}