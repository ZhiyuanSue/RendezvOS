#include <shampoos/mm/vmm.h>
#include <common/string.h>
#include <shampoos/error.h>
#include <modules/log/log.h>
extern u64 *MAP_L1_table, *MAP_L2_table, *MAP_L3_table;
void init_map()
{
        ARCH_PFLAGS_t flags;
        paddr vspace_root = get_current_kernel_vspace_root();
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
static void util_map(paddr p, vaddr v)
/*This function try to map one phy page to virtual page at the MAP_L3_table as a
 * tool to change data*/
{
        ARCH_PFLAGS_t flags;
        flags = arch_decode_flags(3,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        arch_set_L3_entry(p, v, (union L3_entry *)&MAP_L3_table, flags);
        // TODO:flush tlb
}
error_t map(paddr *vspace_root_paddr, u64 ppn, u64 vpn, int level,
            struct pmm *pmm)
{
        int cpu_id = 0; /*for smp, it might map to different L3 table entry*/
        ARCH_PFLAGS_t flags;
        vaddr map_vaddr = map_pages + cpu_id * PAGE_SIZE * 4+PAGE_SIZE;
        paddr p = ppn << 12;
        vaddr v = vpn << 12;
        paddr next_level_paddr;
        bool new_alloc = false;
        /*for a new alloced page, we must memset to all 0, use this flag to
         * decide whether memset*/

        /*=== === === L0 table === === ===*/
        /*some check*/
        if (level != 2 && level != 3) {
                pr_error("[ ERROR ] we only support 2M/4K mapping\n");
                return -EINVAL;
        }
        if (!ppn | !vpn | !pmm) {
                pr_error("[ ERROR ] input arguments error\n");
                return -EINVAL;
        }

        /*for the buddy can only alloc 2M at most*/
        /*if no root page, try to allocator one with the pmm allocator*/
        if (!(*vspace_root_paddr)) {
                // TOOD:lock pmm alloctor
                *vspace_root_paddr = (pmm->pmm_alloc(1, ZONE_NORMAL)) << 12;
                // TODO:unlock pmm allocator
                new_alloc = true;
        } else if (ROUND_DOWN(*vspace_root_paddr, PAGE_SIZE)
                   != *vspace_root_paddr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr in mapping, please check\n");
                return -EINVAL;
        }
        /*map the L0 table to one L3 table entry*/
        util_map(*vspace_root_paddr, map_vaddr);
        if (new_alloc) {
                memset((char *)map_vaddr, 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L0 table*/
        next_level_paddr =
                L0_entry_addr(((union L0_entry *)map_vaddr)[L0_INDEX(v)]);
        if (!next_level_paddr) {
                /*no next level page, need alloc one*/
                // TOOD:lock pmm alloctor
                next_level_paddr = (pmm->pmm_alloc(1, ZONE_NORMAL)) << 12;
                // TODO:unlock pmm allocator
                new_alloc = true;
                flags = arch_decode_flags(0,
                                          PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                                  | PAGE_ENTRY_VALID
                                                  | PAGE_ENTRY_WRITE);
                arch_set_L0_entry(next_level_paddr,
                                  v,
                                  (union L0_entry *)map_vaddr,
                                  flags);
        }
        /*=== === === L1 table === === ===*/
        /*map the L1 table to one L3 table entry*/
		map_vaddr+=PAGE_SIZE;
        util_map(next_level_paddr, map_vaddr);
        if (new_alloc) {
                memset((char *)map_vaddr, 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L1 table*/
        next_level_paddr =
                L1_entry_addr(((union L1_entry *)map_vaddr)[L1_INDEX(v)]);
        if (!next_level_paddr) {
                /*no next level page, need alloc one*/
                // TOOD:lock pmm alloctor
                next_level_paddr = (pmm->pmm_alloc(1, ZONE_NORMAL)) << 12;
                // TODO:unlock pmm allocator
                new_alloc = true;
                flags = arch_decode_flags(1,
                                          PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                                  | PAGE_ENTRY_VALID
                                                  | PAGE_ENTRY_WRITE);
                arch_set_L1_entry(next_level_paddr,
                                  v,
                                  (union L1_entry *)map_vaddr,
                                  flags);
        }
        /*=== === === L2 table === === ===*/
        /*map the L1 table to one L3 table entry*/
		map_vaddr+=PAGE_SIZE;
        util_map(next_level_paddr, map_vaddr);
        if (new_alloc) {
                memset((char *)map_vaddr, 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L2 table*/
        if (level == 2) {
                flags = arch_decode_flags(
                        2,
                        PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                                | PAGE_ENTRY_WRITE | PAGE_ENTRY_HUGE);
                if (!next_level_paddr) {
                        arch_set_L2_entry(
                                p, v, (union L2_entry *)map_vaddr, flags);
                        return 0;
                }
                if (next_level_paddr != p) {
                        pr_error(
                                "[ MAP ] mapping two different physical pages to a same virtual 2M page");
                        pr_error("[ MAP ] arguments: old 0x%x new 0x%x\n",
                                 next_level_paddr,
                                 p);
                        return -EINVAL;
                }
                pr_info("[ MAP ] remap same physical pages to a same virtual 2M page\n");
                return 0;
        } else {
                next_level_paddr = L2_entry_addr(
                        ((union L2_entry *)map_vaddr)[L2_INDEX(v)]);
                if (!next_level_paddr) {
                        /*no next level page, need alloc one*/
                        // TOOD:lock pmm alloctor
                        next_level_paddr = (pmm->pmm_alloc(1, ZONE_NORMAL))
                                           << 12;
                        // TODO:unlock pmm allocator
                        new_alloc = true;
                        flags = arch_decode_flags(
                                2,
                                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);
                        arch_set_L2_entry(next_level_paddr,
                                          v,
                                          (union L2_entry *)map_vaddr,
                                          flags);
                }
        }
        /*=== === === L3 table === === ===*/
        /*map the L1 table to one L3 table entry*/
		map_vaddr+=PAGE_SIZE;
        util_map(next_level_paddr, map_vaddr);
        if (new_alloc) {
                memset((char *)map_vaddr, 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L3 table*/
        next_level_paddr =
                L3_entry_addr(((union L3_entry *)map_vaddr)[L3_INDEX(v)]);
        if (!next_level_paddr) { /*seems more likely*/
                /*we give the ppn, and no need to alloc another page*/
                flags = arch_decode_flags(3,
                                          PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                                  | PAGE_ENTRY_VALID
                                                  | PAGE_ENTRY_WRITE);
                arch_set_L3_entry(p, v, (union L3_entry *)map_vaddr, flags);
                return 0;
        }
        if (next_level_paddr != p) {
                pr_error(
                        "[ MAP ] mapping two different physical pages to a same virtual 4K page");
                pr_error("[ MAP ] arguments: old 0x%x new 0x%x\n",
                         next_level_paddr,
                         p);
                return -EINVAL;
        }
        pr_info("[ MAP ] remap same physical pages to a same virtual 4K page\n");
        // TODO:flush tlb of addr v
        return 0;
}