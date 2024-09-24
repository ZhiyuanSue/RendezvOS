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
        ARCH_PFLAGS_t flags = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        vaddr map_vaddr = map_pages + cpu_id * PAGE_SIZE * 4;
        paddr p = ppn << 12;
        vaddr v = vpn << 12;
        u64 pt_entry = 0;
        paddr next_level_paddr = 0;
        int pmm_res = 0;
        bool new_alloc = false;
        /*for a new alloced page, we must memset to all 0, use this flag to
         * decide whether memset*/
        /*flush all the tlbs of those 4 pages*/
        arch_tlb_invalidate_range(map_vaddr, map_vaddr + PAGE_SIZE * 4);

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
                pmm_res = pmm->pmm_alloc(1, ZONE_NORMAL);
                // TODO:unlock pmm allocator
                if (pmm_res <= 0) {
                        pr_error("[ ERROR ] try alloc vspace root ppn fail\n");
                        return -ENOMEM;
                }
                *vspace_root_paddr = (pmm_res) << 12;
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
                pmm_res = pmm->pmm_alloc(1, ZONE_NORMAL);
                // TODO:unlock pmm allocator
                if (pmm_res <= 0) {
                        pr_error("[ ERROR ] try alloc ppn fail\n");
                        return -ENOMEM;
                }
                next_level_paddr = (pmm_res) << 12;
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
        map_vaddr += PAGE_SIZE;
        util_map(next_level_paddr, map_vaddr);
        if (new_alloc) {
                memset((char *)map_vaddr, 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L1 table*/
        next_level_paddr =
                L1_entry_addr(((union L1_entry *)map_vaddr)[L1_INDEX(v)]);
        pt_entry = ((union L1_entry *)map_vaddr)[L1_INDEX(v)].entry;
        if (!next_level_paddr) {
                /*no next level page, need alloc one*/
                // TOOD:lock pmm alloctor
                pmm_res = pmm->pmm_alloc(1, ZONE_NORMAL);
                // TODO:unlock pmm allocator
                if (pmm_res <= 0) {
                        pr_error("[ ERROR ] try alloc ppn fail\n");
                        return -ENOMEM;
                }
                next_level_paddr = (pmm_res) << 12;
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
        map_vaddr += PAGE_SIZE;
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
                        /*
                           there might have one special case: if I map a 4K page
                           and then unmap it, the 2M page entry still there, and
                           then if I try to map it to a 2M page it will cause an
                           error, but the 4K page is unmapped, so it must be
                           right logically

                           to avoid this problem, here we need to check the
                           valid and whether is the end page table of this entry
                           and map the level 3 page table to the next entry(if
                           it's valid), and then check all the entry in the
                           level 3 page table

                           1/if it's not valid and not the final page table but
                           still have value, we do not let the page table swap
                           out to the disk, it must be an error

                           2/if it's not valid but have value and is final, it
                           means the 2M page is swap out, a remap happend

                           3/if it's valid and not the final page table, we need
                           to map and check all the entry's

                           4/if it's valid(must have value) and the final page
                           table, we also have a remap event
                        */
                        entry_flags = arch_encode_flags(2, pt_entry);
                        if ((entry_flags & PAGE_ENTRY_VALID)
                            && !is_final_level_pt(2, entry_flags)) {
                                vaddr pre_map_vaddr = map_vaddr + PAGE_SIZE;
                                util_map(next_level_paddr, pre_map_vaddr);
                                bool have_filled_entry = false;
                                for (;
                                     pre_map_vaddr < map_vaddr + PAGE_SIZE * 2;
                                     pre_map_vaddr += sizeof(union L3_entry)) {
                                        if (((union L3_entry *)pre_map_vaddr)
                                                    ->entry) {
                                                have_filled_entry = true;
                                                break;
                                        }
                                }
                                if (have_filled_entry) {
                                        pr_error(
                                                "[ MAP ] mapping 2M page have had a mapped level 3 page table and have a existed 4K entry\n");
                                        return -EINVAL;
                                }
                                pmm_res =
                                        pmm->pmm_free(PPN(next_level_paddr), 1);
                                if (pmm_res) {
                                        pr_error(
                                                "[ MAP ] pmm free error with a ppn 0x%x\n",
                                                PPN(next_level_paddr));
                                        return -EINVAL;
                                }
                                arch_set_L2_entry(p,
                                                  v,
                                                  (union L2_entry *)map_vaddr,
                                                  flags);
                        } else {
                                pr_error(
                                        "[ MAP ] mapping two different physical pages to a same virtual 2M page\n");
                                pr_error(
                                        "[ MAP ] arguments: old 0x%x new 0x%x\n",
                                        next_level_paddr,
                                        p);
                                return -EINVAL;
                        }
                }
                pr_info("[ MAP ] remap same physical pages to a same virtual 2M page\n");
                return 0;
        } else {
                next_level_paddr = L2_entry_addr(
                        ((union L2_entry *)map_vaddr)[L2_INDEX(v)]);
                if (!next_level_paddr) {
                        /*no next level page, need alloc one*/
                        // TOOD:lock pmm alloctor
                        pmm_res = pmm->pmm_alloc(1, ZONE_NORMAL);
                        // TODO:unlock pmm allocator
                        if (pmm_res <= 0) {
                                pr_error("[ ERROR ] try alloc ppn fail\n");
                                return -ENOMEM;
                        }
                        next_level_paddr = (pmm_res) << 12;
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
        map_vaddr += PAGE_SIZE;
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
                        "[ MAP ] mapping two different physical pages to a same virtual 4K page\n");
                pr_error("[ MAP ] arguments: old 0x%x new 0x%x\n",
                         next_level_paddr,
                         p);
                return -EINVAL;
        }
        pr_info("[ MAP ] remap same physical pages to a same virtual 4K page\n");
        arch_tlb_invalidate(v);
        return 0;
}

error_t unmap(paddr vspace_root_paddr, u64 vpn, size_t page_num)
{
        return 0;
}