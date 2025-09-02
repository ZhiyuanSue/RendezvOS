#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>
extern u64 L0_table;
extern u64 *MAP_L1_table, *MAP_L2_table, *MAP_L3_table;
spin_lock kspace_spin_lock_ptr = NULL;
DEFINE_PER_CPU(struct spin_lock_t, vspace_spin_lock);
void sys_init_map()
{
        ARCH_PFLAGS_t flags;
        paddr vspace_root_addr = arch_get_current_kernel_vspace_root();
        flags = arch_decode_flags(0,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        arch_set_L0_entry(
                KERNEL_VIRT_TO_PHY((vaddr)&MAP_L1_table),
                map_pages,
                (union L0_entry *)KERNEL_PHY_TO_VIRT(vspace_root_addr),
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
}
void init_map(struct map_handler *handler, int cpu_id, int pt_zone,
              struct pmm *pmm)
{
        handler->cpu_id = cpu_id;
        handler->pmm = pmm;
        handler->page_table_zone = pt_zone;
        vaddr map_vaddr = map_pages + cpu_id * PAGE_SIZE * 4;
        size_t alloced_page_number;
        for (int i = 0; i < 4; i++) {
                handler->map_vaddr[i] = map_vaddr;
                handler->handler_ppn[i] =
                        pmm->pmm_alloc(1, pt_zone, &alloced_page_number);
                if (handler->handler_ppn[i] <= 0 || alloced_page_number != 1) {
                        pr_error("[ERROR] init map no ppn can alloc\n");
                }
                map_vaddr += PAGE_SIZE;
        }
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
}
error_t map(VSpace *vs, u64 ppn, u64 vpn, int level, ENTRY_FLAGS_t eflags,
            struct map_handler *handler, spin_lock *lock)
{
        ARCH_PFLAGS_t flags = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        paddr p = ppn << 12;
        vaddr v = vpn << 12;
        u64 pt_entry = 0;
        paddr next_level_paddr = 0;
        i64 pmm_res = 0;
        bool new_alloc = false;
        error_t res = 0;
        size_t alloced_page_number;
        /*for a new alloced page, we must memset to all 0, use this flag to
         * decide whether memset*/

        /*=== === === L0 table === === ===*/
        /*some check*/
        if (level != 2 && level != 3) {
                pr_error("[ ERROR ] we only support 2M/4K mapping\n");
                res = -E_IN_PARAM;
                goto map_fail;
        }
        if (!ppn | !vpn | !handler) {
                pr_error("[ ERROR ] input arguments error\n");
                res = -E_IN_PARAM;
                goto map_fail;
        }

        /*for the buddy can only alloc 2M at most*/
        /*
                        if no root page
                        the function was tried to allocator new one with the pmm
           allocator but I change the logic, we directly think it's illegal and
           it must use new_vs_root function to generate a new one
                */
        if (!(vs->vspace_root_addr)) {
                pr_error("[ ERROR ] No vs root\n");
                res = -E_RENDEZVOS;
                goto map_fail;
        } else if (ROUND_DOWN(vs->vspace_root_addr, PAGE_SIZE)
                   != vs->vspace_root_addr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr in mapping, please check\n");
                res = -E_RENDEZVOS;
                goto map_fail;
        }
        /*map the L0 table to one L3 table entry*/
        util_map(vs->vspace_root_addr, handler->map_vaddr[0]);
        if (new_alloc) {
                memset((char *)(handler->map_vaddr[0]), 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L0 table*/
        if (lock)
                lock_mcs(lock, &per_cpu(vspace_spin_lock, handler->cpu_id));
        /*
                Do not try to add a small lock between l2 and l3 table
                for it might memset to clean it
                so actually all the code need to be locked
        */
        next_level_paddr = L0_entry_addr(
                ((union L0_entry *)(handler->map_vaddr[0]))[L0_INDEX(v)]);
        if (!next_level_paddr) {
                /*
                   no next level page, need alloc one
                   but this alloc might a bit complex under
                   kernel ，consider one thing: if the vs1 kernel add a new
                   entry at level 0 but the vs0 didn't add and now a vs change
                   happen,the kernel might find it miss something so we just
                   sync it with the init level 0 table . besides, we need to add
                   a logic like that: if a pagetable fault happened under kernel
                   we must check whether that is caused by
                   sync first and then call the page fault handler provide by
                   upper kernel module
                */
                if (handler->handler_ppn[1] <= 0) {
                        pr_error("[ ERROR ] L1 try alloc ppn fail\n");
                        res = -E_RENDEZVOS;
                        goto map_l0_fail;
                }
                next_level_paddr = PADDR(handler->handler_ppn[1]);
                handler->handler_ppn[1] = -E_RENDEZVOS;
                new_alloc = true;
                flags = arch_decode_flags(0, eflags);
                arch_set_L0_entry(next_level_paddr,
                                  v,
                                  (union L0_entry *)(handler->map_vaddr[0]),
                                  flags);
                /*sync with the root vs*/
                if (vs->vspace_root_addr
                    != KERNEL_VIRT_TO_PHY((vaddr)(&L0_table))) {
                        arch_set_L0_entry(next_level_paddr,
                                          v,
                                          (union L0_entry *)(&L0_table),
                                          flags);
                }
        }
        /*=== === === L1 table === === ===*/
        /*map the L1 table to one L3 table entry*/
        util_map(next_level_paddr, handler->map_vaddr[1]);
        if (new_alloc) {
                memset((char *)(handler->map_vaddr[1]), 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L1 table*/
        next_level_paddr = L1_entry_addr(
                ((union L1_entry *)(handler->map_vaddr[1]))[L1_INDEX(v)]);
        if (!next_level_paddr) {
                /*no next level page, need alloc one*/
                if (handler->handler_ppn[2] <= 0) {
                        pr_error("[ ERROR ] L2 try alloc ppn fail\n");
                        res = -E_RENDEZVOS;
                        goto map_l1_fail;
                }
                next_level_paddr = PADDR(handler->handler_ppn[2]);
                handler->handler_ppn[2] = -E_RENDEZVOS;
                new_alloc = true;
                flags = arch_decode_flags(1, eflags);
                arch_set_L1_entry(next_level_paddr,
                                  v,
                                  (union L1_entry *)(handler->map_vaddr[1]),
                                  flags);
        }
        /*=== === === L2 table === === ===*/
        /*map the L1 table to one L3 table entry*/
        util_map(next_level_paddr, handler->map_vaddr[2]);
        if (new_alloc) {
                memset((char *)(handler->map_vaddr[2]), 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L2 table*/
        if (level == 2) {
                /*ppn and vpn must be 2M aligned*/
                if (ROUND_DOWN(v, MIDDLE_PAGE_SIZE) != v
                    || ROUND_DOWN(p, MIDDLE_PAGE_SIZE) != p) {
                        pr_error(
                                "[ ERROR ] the ppn and vpn must be 2M aligned\n");
                        res = -E_IN_PARAM;
                        goto map_fail;
                }
                flags = arch_decode_flags(2, PAGE_ENTRY_HUGE | eflags);
                next_level_paddr = L2_entry_addr(((
                        union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)]);
                if (!next_level_paddr) {
                        /*we must let the ppn and vpn all 2M aligned*/
                        if ((vpn & mask_9_bit) || (ppn & mask_9_bit)) {
                                pr_error(
                                        "the vpn and ppn must all 2M aligned\n");
                                res = -E_RENDEZVOS;
                                goto map_l2_fail;
                        }
                        arch_set_L2_entry(
                                p,
                                v,
                                (union L2_entry *)(handler->map_vaddr[2]),
                                flags);
                        res = 0;
                        goto map_succ;
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
                        pt_entry = ((
                                union L2_entry
                                        *)(handler->map_vaddr[2]))[L2_INDEX(v)]
                                           .entry;
                        entry_flags = arch_encode_flags(2, pt_entry);
                        if ((entry_flags & PAGE_ENTRY_VALID)
                            && !is_final_level_pt(2, entry_flags)) {
                                vaddr pre_map_vaddr = ROUND_DOWN(
                                        ((handler->map_vaddr[2]) + PAGE_SIZE),
                                        PAGE_SIZE);
                                util_map(next_level_paddr, pre_map_vaddr);
                                bool have_filled_entry = false;
                                for (; pre_map_vaddr
                                       < ROUND_UP(((handler->map_vaddr[2])
                                                   + PAGE_SIZE),
                                                  PAGE_SIZE);
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
                                        res = -E_RENDEZVOS;
                                        arch_tlb_invalidate_page(
                                                0,
                                                ROUND_DOWN(
                                                        ((handler->map_vaddr[2])
                                                         + PAGE_SIZE),
                                                        PAGE_SIZE));
                                        goto map_l2_fail;
                                }
                                lock_mcs(&handler->pmm->spin_ptr,
                                         &per_cpu(pmm_spin_lock,
                                                  handler->cpu_id));
                                pmm_res = handler->pmm->pmm_free(
                                        (i64)PPN(next_level_paddr), 1);
                                unlock_mcs(&handler->pmm->spin_ptr,
                                           &per_cpu(pmm_spin_lock,
                                                    handler->cpu_id));
                                if (pmm_res) {
                                        pr_error(
                                                "[ MAP ] pmm free error with a ppn 0x%x\n",
                                                PPN(next_level_paddr));
                                        res = -E_RENDEZVOS;
                                        goto map_l2_fail;
                                }
                                arch_set_L2_entry(
                                        p,
                                        v,
                                        (union L2_entry
                                                 *)(handler->map_vaddr[2]),
                                        flags);
                                res = 0;
                                goto map_succ;
                        } else {
                                pr_error(
                                        "[ MAP ] mapping two different physical pages to a same virtual 2M page\n");
                                pr_error(
                                        "[ MAP ] arguments: old 0x%x new 0x%x\n",
                                        next_level_paddr,
                                        p);
                                res = -E_RENDEZVOS;
                                goto map_l2_fail;
                        }
                }
                pr_info("[ MAP ] remap same physical pages to a same virtual 2M page\n");
                res = 0;
                goto map_succ;
        } else {
                next_level_paddr = L2_entry_addr(((
                        union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)]);
                if (!next_level_paddr) {
                        /*no next level page, need alloc one*/
                        if (handler->handler_ppn[3] <= 0) {
                                pr_error("[ ERROR ] L3 try alloc ppn fail\n");
                                res = -E_RENDEZVOS;
                                goto map_l2_fail;
                        }
                        next_level_paddr = PADDR(handler->handler_ppn[3]);
                        handler->handler_ppn[3] = -E_RENDEZVOS;
                        new_alloc = true;
                        flags = arch_decode_flags(2, eflags);
                        arch_set_L2_entry(
                                next_level_paddr,
                                v,
                                (union L2_entry *)(handler->map_vaddr[2]),
                                flags);
                }
        }
        /*=== === === L3 table === === ===*/
        /*map the L1 table to one L3 table entry*/
        util_map(next_level_paddr, handler->map_vaddr[3]);
        if (new_alloc) {
                memset((char *)(handler->map_vaddr[3]), 0, PAGE_SIZE);
                new_alloc = false;
        }
        /*use map util table to change the L3 table*/
        next_level_paddr = L3_entry_addr(
                ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)]);
        if (!next_level_paddr) { /*seems more likely*/
                /*we give the ppn, and no need to alloc another page*/
                flags = arch_decode_flags(3, eflags);
                arch_set_L3_entry(
                        p, v, (union L3_entry *)(handler->map_vaddr[3]), flags);
                goto map_succ;
        }
        if (next_level_paddr != p) {
                pr_error(
                        "[ MAP ] mapping two different physical pages to a same virtual 4K page\n");
                pr_error("[ MAP ] arguments: old 0x%x new 0x%x\n",
                         next_level_paddr,
                         p);
                res = -E_RENDEZVOS;
                goto map_l3_fail;
        }
        pr_error(
                "[ MAP ] remap same physical pages to a same virtual 4K page\n");
        res = -E_RENDEZVOS;
map_succ:
        arch_tlb_invalidate_page(vs->vspace_id, v);
map_l3_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[3]);
map_l2_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[2]);
map_l1_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
map_l0_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
        if (lock)
                unlock_mcs(lock, &per_cpu(vspace_spin_lock, handler->cpu_id));
map_fail:
        for (int i = 0; i < 4; i++) {
                if (handler->handler_ppn[i] == -E_RENDEZVOS) {
                        lock_mcs(&handler->pmm->spin_ptr,
                                 &per_cpu(pmm_spin_lock, handler->cpu_id));
                        handler->handler_ppn[i] = handler->pmm->pmm_alloc(
                                1,
                                handler->page_table_zone,
                                &alloced_page_number);
                        unlock_mcs(&handler->pmm->spin_ptr,
                                   &per_cpu(pmm_spin_lock, handler->cpu_id));
                }
        }
        return res;
}

error_t unmap(VSpace *vs, u64 vpn, struct map_handler *handler, spin_lock *lock)
{
        if (lock)
                lock_mcs(lock, &per_cpu(vspace_spin_lock, handler->cpu_id));
        vaddr v = vpn << 12;
        paddr next_level_paddr = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        union L0_entry L0_E;
        union L1_entry L1_E;
        union L2_entry L2_E;
        union L3_entry L3_E;
        error_t res = 0;
        if (!vs->vspace_root_addr || !vpn) {
                pr_error("[ ERROR ] unmap input is not right\n");
                res = -E_IN_PARAM;
                goto unmap_fail;
        } else if (ROUND_DOWN(vs->vspace_root_addr, PAGE_SIZE)
                   != vs->vspace_root_addr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr 0x%x in mapping, please check\n",
                        vs->vspace_root_addr);
                res = -E_IN_PARAM;
                goto unmap_fail;
        }
        /*=== === === L0 table === === ===*/
        util_map(vs->vspace_root_addr, handler->map_vaddr[0]);
        L0_E = ((union L0_entry *)(handler->map_vaddr[0]))[L0_INDEX(v)];
        entry_flags = arch_encode_flags(0, (ARCH_PFLAGS_t)L0_E.entry);
        next_level_paddr = L0_entry_addr(L0_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) /*we will
                                                                       not swap
                                                                       out the
                                                                       l0 page*/
        {
                pr_error(
                        "[ ERROR ] L0 entry not mapped, entry is 0x%x, unmap error\n",
                        L0_E);
                res = -E_RENDEZVOS;
                goto unmap_l0_fail;
        }

        /*=== === === L1 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[1]);
        L1_E = ((union L1_entry *)(handler->map_vaddr[1]))[L1_INDEX(v)];
        entry_flags = arch_encode_flags(1, (ARCH_PFLAGS_t)L1_E.entry);
        next_level_paddr = L1_entry_addr(L1_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) /*we will
                                                                       not swap
                                                                       out the
                                                                       l1 page*/
        {
                pr_error("[ ERROR ] L1 entry not mapped, unmap error\n");
                res = -E_RENDEZVOS;
                goto unmap_l1_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                pr_error("[ ERROR ] we do not use 1G huge page, unmap error\n");
                res = -E_RENDEZVOS;
                goto unmap_l1_fail;
        }

        /*=== === === L2 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[2]);
        L2_E = ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)];
        entry_flags = arch_encode_flags(2, (ARCH_PFLAGS_t)L2_E.entry);
        next_level_paddr = L2_entry_addr(L2_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) /*we will
                                                                       not swap
                                                                       out the
                                                                       l2 page*/
        {
                pr_error("[ ERROR ] L2 entry not mapped, unmap error\n");
                res = -E_RENDEZVOS;
                goto unmap_l2_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                /*check the vpn 2M aligned*/
                if (vpn & mask_9_bit) {
                        pr_error(
                                "[ ERROR ] try to unmap a 2M entry %x %x %x %d, but vpn is not 2M  aligned\n",
                                L2_E.entry,
                                entry_flags,
                                v,
                                per_cpu(cpu_number, handler->cpu_id));
                        res = -E_RENDEZVOS;
                        goto unmap_l2_fail;
                }
                ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)].entry =
                        0;
                goto unmap_succ;
        }

        /*=== === === L3 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[3]);
        L3_E = ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)];
        entry_flags = arch_encode_flags(3, (ARCH_PFLAGS_t)L3_E.entry);
        next_level_paddr = L3_entry_addr(L3_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) /*we will
                                                                       not swap
                                                                       out the
                                                                       l3 page*/
        {
                pr_error("[ ERROR ] L3 entry not mapped, unmap error\n");
                res = -E_RENDEZVOS;
                goto unmap_l3_fail;
        }
        ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)].entry = 0;
unmap_succ:
        arch_tlb_invalidate_page(vs->vspace_id, v);
unmap_l3_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[3]);
unmap_l2_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[2]);
unmap_l1_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
unmap_l0_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
unmap_fail:
        if (lock)
                unlock_mcs(lock, &per_cpu(vspace_spin_lock, handler->cpu_id));
        return res;
}
// find one vpn have mapped in one vspace or not
paddr have_mapped(VSpace *vs, u64 vpn, struct map_handler *handler)
{
        vaddr v = vpn << 12;
        paddr next_level_paddr = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        union L0_entry L0_E;
        union L1_entry L1_E;
        union L2_entry L2_E;
        union L3_entry L3_E;
        if (!vs || !vs->vspace_root_addr || !vpn) {
                pr_error("[ ERROR ] check input is not right\n");
                next_level_paddr = 0;
                goto have_mapped_fail;
        } else if (ROUND_DOWN(vs->vspace_root_addr, PAGE_SIZE)
                   != vs->vspace_root_addr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr 0x%x in mapping, please check\n",
                        vs->vspace_root_addr);
                next_level_paddr = 0;
                goto have_mapped_fail;
        }
        /*=== === === L0 table === === ===*/
        util_map(vs->vspace_root_addr, handler->map_vaddr[0]);
        L0_E = ((union L0_entry *)(handler->map_vaddr[0]))[L0_INDEX(v)];
        entry_flags = arch_encode_flags(0, (ARCH_PFLAGS_t)L0_E.entry);
        next_level_paddr = L0_entry_addr(L0_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) {
                next_level_paddr = 0;
                goto have_mapped_l0_fail;
        }

        /*=== === === L1 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[1]);
        L1_E = ((union L1_entry *)(handler->map_vaddr[1]))[L1_INDEX(v)];
        entry_flags = arch_encode_flags(1, (ARCH_PFLAGS_t)L1_E.entry);
        next_level_paddr = L1_entry_addr(L1_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) {
                next_level_paddr = 0;
                goto have_mapped_l1_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                pr_error("[ ERROR ] we do not use 1G huge page, check error\n");
                next_level_paddr = 0;
                goto have_mapped_l1_fail;
        }

        /*=== === === L2 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[2]);
        L2_E = ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)];
        entry_flags = arch_encode_flags(2, (ARCH_PFLAGS_t)L2_E.entry);
        next_level_paddr = L2_entry_addr(L2_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) {
                next_level_paddr = 0;
                goto have_mapped_l2_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                goto have_mapped_succ;
        }

        /*=== === === L3 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[3]);
        L3_E = ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)];
        entry_flags = arch_encode_flags(3, (ARCH_PFLAGS_t)L3_E.entry);
        next_level_paddr = L3_entry_addr(L3_E);
        if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID))
                next_level_paddr = 0;
have_mapped_succ:
        arch_tlb_invalidate_page(vs->vspace_id, v);
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[3]);
have_mapped_l2_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[2]);
have_mapped_l1_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
have_mapped_l0_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
have_mapped_fail:
        return next_level_paddr;
}
paddr new_vs_root(paddr old_vs_root_paddr, struct map_handler *handler)
{
        /*no need to lock here*/
        paddr vs_root = 0;
        size_t alloced_page_number;
        if (handler->handler_ppn[0] <= 0) {
                pr_error("[ ERROR ] L0 try alloc vspace root ppn fail\n");
                goto new_vs_root_fail;
        }
        vs_root = PADDR(handler->handler_ppn[0]);
        handler->handler_ppn[0] = -E_RENDEZVOS;
        util_map(vs_root, handler->map_vaddr[0]);

        /*
                memcpy the kernel part, which copy from the root vs kernel part
        */

        memcpy((void *)(handler->map_vaddr[0] + PAGE_SIZE / 2),
               (void *)((vaddr)(&L0_table) + PAGE_SIZE / 2),
               PAGE_SIZE / 2);

        /*
                try to memcpy the user part or just clean it
        */
        if (old_vs_root_paddr) {
                util_map(old_vs_root_paddr, handler->map_vaddr[1]);
                /*TODO:might consider the COW*/
                memcpy((void *)(handler->map_vaddr[0]),
                       (void *)(handler->map_vaddr[1]),
                       PAGE_SIZE / 2);
                arch_tlb_invalidate_page(0, handler->map_vaddr[1]);
        } else {
                memset((void *)(handler->map_vaddr[0]), 0, PAGE_SIZE / 2);
        }

        arch_tlb_invalidate_page(0, handler->map_vaddr[0]);
        return vs_root;
new_vs_root_fail:
        if (handler->handler_ppn[0] == -E_RENDEZVOS) {
                lock_mcs(&handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, handler->cpu_id));
                handler->handler_ppn[0] = handler->pmm->pmm_alloc(
                        1, handler->page_table_zone, &alloced_page_number);
                unlock_mcs(&handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, handler->cpu_id));
        }
        return vs_root;
}