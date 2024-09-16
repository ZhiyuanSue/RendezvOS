#include <shampoos/mm/vmm.h>
#include <shampoos/error.h>
#include <modules/log/log.h>
extern u64 MAP_L1_table, MAP_L2_table, MAP_L3_table;
void init_map()
{
        paddr vspace_root = get_current_kernel_vspace_root();
        // TODO:check the parameter
        arch_set_L0_entry(KERNEL_VIRT_TO_PHY(MAP_L1_table),
                          map_pages,
                          (union L0_entry *)vspace_root,
                          0);
        arch_set_L1_entry(KERNEL_VIRT_TO_PHY(MAP_L2_table),
                          map_pages,
                          (union L1_entry *)&MAP_L1_table,
                          0);
        arch_set_L2_entry(KERNEL_VIRT_TO_PHY(MAP_L3_table),
                          map_pages,
                          (union L2_entry *)&MAP_L2_table,
                          0);
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
}