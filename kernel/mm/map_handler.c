#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>
extern u64 L0_table[512];
extern u64 *MAP_L1_table, *MAP_L2_table, *MAP_L3_table;
void sys_init_map(struct pmm *pmm)
{
        ARCH_PFLAGS_t flags;
        paddr vspace_root_addr = arch_get_current_kernel_vspace_root();
        flags = arch_decode_flags(0,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        /*set map handler page table*/
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
        /*set kernel L1 tables, it must be BSP alloc and no other AP is alive.
         * No need to lock*/
        flags = arch_decode_flags(0,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE
                                          | PAGE_ENTRY_EXEC);
        for (int index = 256; index < 512; index++) {
                union L0_entry *L0_E =
                        ((union L0_entry *)KERNEL_PHY_TO_VIRT(vspace_root_addr))
                        + index;
                if (L0_E->entry != 0)
                        continue;
                size_t need_page_number = 1;
                size_t alloced_page_number;
                ppn_t ppn = pmm->pmm_alloc(
                        pmm, need_page_number, &alloced_page_number);
                if (alloced_page_number == need_page_number
                    && !invalid_ppn(ppn)) {
                        arch_set_L0_entry(PADDR(ppn),
                                          KERNEL_VIRT_OFFSET
                                                  + (index - 256)
                                                            * HUGE_PAGE_SIZE,
                                          (union L0_entry *)KERNEL_PHY_TO_VIRT(
                                                  vspace_root_addr),
                                          flags);
                }
        }
}
void init_map(struct map_handler *handler, cpu_id_t cpu_id, struct pmm *pmm)
{
        if (!handler || !pmm) {
                return;
        }
        handler->cpu_id = cpu_id;
        handler->pmm = pmm;
        vaddr map_vaddr = map_pages + (vaddr)cpu_id * PAGE_SIZE * 4;
        size_t alloced_page_number;
        for (int i = 0; i < 4; i++) {
                handler->map_vaddr[i] = map_vaddr;
                handler->handler_ppn[i] =
                        pmm->pmm_alloc(pmm, 1, &alloced_page_number);
                if (invalid_ppn(handler->handler_ppn[i])
                    || alloced_page_number != 1) {
                        pr_error("[ERROR] init map no ppn can alloc\n");
                        /* Rollback: free already allocated pages */
                        for (int j = 0; j < i; j++) {
                                if (!invalid_ppn(handler->handler_ppn[j])) {
                                        pmm->pmm_free(pmm,
                                                      handler->handler_ppn[j],
                                                      1);
                                        handler->handler_ppn[j] = -E_RENDEZVOS;
                                }
                        }
                        return;
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

vaddr map_handler_map_slot(struct map_handler *handler, int slot_id, ppn_t ppn)
{
        if (!handler)
                return 0;
        if (slot_id < 0 || slot_id >= 4)
                return 0;
        if ((PADDR(ppn) & (PAGE_SIZE - 1)) != 0)
                return 0;
        vaddr v = handler->map_vaddr[slot_id];
        if (!v)
                return 0;
        util_map(PADDR(ppn), v);
        arch_tlb_invalidate_page(0, v);
        return v;
}

void map_handler_unmap_slot(struct map_handler *handler, int slot_id)
{
        if (!handler)
                return;
        if (slot_id < 0 || slot_id >= 4)
                return;
        vaddr v = handler->map_vaddr[slot_id];
        if (!v)
                return;
        arch_set_L3_entry(
                0, v, (union L3_entry *)&MAP_L3_table, PAGE_ENTRY_NONE);
        arch_tlb_invalidate_page(0, v);
}

error_t map_handler_copy_data_range(struct map_handler *handler,
                                    paddr dst_paddr, paddr src_paddr, u64 len)
{
        if (!handler || len == 0)
                return -E_IN_PARAM;

        /*
         * Copy potentially unaligned ranges by mapping each involved physical
         * page into a mapping-window slot.
         */
        while (len) {
                const paddr src_page = ROUND_DOWN(src_paddr, PAGE_SIZE);
                const paddr dst_page = ROUND_DOWN(dst_paddr, PAGE_SIZE);
                const u64 src_off = (u64)(src_paddr - src_page);
                const u64 dst_off = (u64)(dst_paddr - dst_page);

                /* Copy until either src or dst page boundary. */
                u64 chunk = PAGE_SIZE - src_off;
                u64 dst_room = PAGE_SIZE - dst_off;
                if (dst_room < chunk)
                        chunk = dst_room;
                if (chunk > len)
                        chunk = len;

                /* we must promise the copy must be right and if not, it must
                 * crash quickly, so there's a problem, if the calculate is
                 * wrong, and the memcpy cross the page boundary and we use slot
                 * 0 and 1, it will overwrite the slot 1's data so we use 0 and
                 * 2 slot, not 0 and 1 for those two pages are adjacent， It's
                 * defensive programming
                 */
                vaddr dst_kva = map_handler_map_slot(handler, 0, PPN(dst_page));
                vaddr src_kva = map_handler_map_slot(handler, 2, PPN(src_page));
                if (!dst_kva || !src_kva) {
                        map_handler_unmap_slot(handler, 2);
                        map_handler_unmap_slot(handler, 0);
                        return -E_RENDEZVOS;
                }

                memcpy((void *)(dst_kva + dst_off),
                       (const void *)(src_kva + src_off),
                       chunk);

                /* Keep window clean to avoid leaking stale mappings. */
                map_handler_unmap_slot(handler, 2);
                map_handler_unmap_slot(handler, 0);

                src_paddr += chunk;
                dst_paddr += chunk;
                len -= chunk;
        }

        return REND_SUCCESS;
}
error_t map(VS_Common *vs, ppn_t ppn, vpn_t vpn, int level,
            ENTRY_FLAGS_t eflags, struct map_handler *handler)
{
        ARCH_PFLAGS_t flags = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        paddr p = PADDR(ppn);
        vaddr v = VADDR(vpn);
        u64 pt_entry = 0;
        paddr next_level_paddr = 0;
        i64 pmm_res = 0;
        bool new_alloc = false;
        error_t res = REND_SUCCESS;
        size_t alloced_page_number;
        const bool allow_remap = (eflags & PAGE_ENTRY_REMAP) != 0;
        eflags = clear_mask_u64(eflags, PAGE_ENTRY_REMAP);
        /*for a new alloced page, we must memset to all 0, use this flag to
         * decide whether memset*/

        if (!vs || !handler || !handler->pmm) {
                return -E_IN_PARAM;
        }
        if (!vs_common_is_table_vspace(vs)) {
                pr_error("[ MAP ] ERROR: map called with KERNEL_HEAP_REF vs\n");
                return -E_IN_PARAM;
        }
        /*=== === === L0 table === === ===*/
        /*some check*/
        if (level != 2 && level != 3) {
                pr_error("[ ERROR ] we only support 2M/4K mapping\n");
                res = -E_IN_PARAM;
                goto map_fail;
        }
        if (invalid_ppn(ppn) || !vpn || !handler) {
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
        /*use map util table to change the L0 table*/

        lock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
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
                   FIX:it's impossible to boardcast, so we have to alloc all the
                   256 L1 tables first, see sys_init_map
                */
                if (invalid_ppn(handler->handler_ppn[1])) {
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
                if (invalid_ppn(handler->handler_ppn[2])) {
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
                                "[ ERROR ] the ppn and vpn must be 2M aligned p %lx v%lx\n",
                                p,
                                v);
                        res = -E_IN_PARAM;
                        goto map_l0_fail;
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
                                /* Case 3: Valid L3 page table (4K mappings
                                 * exist) */
                                vaddr l3_scan_vaddr = handler->map_vaddr[3];
                                util_map(next_level_paddr, l3_scan_vaddr);

                                /* Scan L3 table to check if any 4K entries are
                                 * in use */
                                bool have_filled_entry = false;
                                for (vaddr vaddr_iter = l3_scan_vaddr;
                                     vaddr_iter < l3_scan_vaddr + PAGE_SIZE;
                                     vaddr_iter += sizeof(union L3_entry)) {
                                        if (((union L3_entry *)vaddr_iter)
                                                    ->entry) {
                                                have_filled_entry = true;
                                                break;
                                        }
                                }

                                if (have_filled_entry) {
                                        /* L3 page table has active 4K mappings
                                         * - cannot upgrade to 2M */
                                        pr_error(
                                                "[ MAP ] cannot map 2M page: L3 page table has active 4K entries (old: 0x%lx, new: 0x%lx)\n",
                                                next_level_paddr,
                                                p);
                                        res = -E_RENDEZVOS;
                                        arch_tlb_invalidate_page(
                                                0,
                                                ROUND_DOWN(
                                                        ((handler->map_vaddr[2])
                                                         + PAGE_SIZE),
                                                        PAGE_SIZE));
                                        goto map_l2_fail;
                                }

                                /* L3 page table is completely empty - safe to
                                 * free and upgrade to 2M */
                                struct pmm *pmm_ptr = handler->pmm;
                                pmm_res = pmm_ptr->pmm_free(
                                        pmm_ptr, (i64)PPN(next_level_paddr), 1);
                                if (pmm_res) {
                                        pr_error(
                                                "[ MAP ] failed to free empty L3 page table (ppn: 0x%lx)\n",
                                                PPN(next_level_paddr));
                                        res = -E_RENDEZVOS;
                                        goto map_l2_fail;
                                }
                                pr_debug(
                                        "[ MAP ] upgraded empty L3 page table to 2M mapping\n");

                                /* Establish the new 2M mapping */
                                arch_set_L2_entry(
                                        p,
                                        v,
                                        (union L2_entry
                                                 *)(handler->map_vaddr[2]),
                                        flags);
                                goto map_succ;

                        } else if (entry_flags & PAGE_ENTRY_VALID) {
                                /* Case 4: Valid L2 entry mapping another 2M
                                 * page */
                                pr_error(
                                        "[ MAP ] cannot map 2M page: different physical page already mapped (old: 0x%lx, new: 0x%lx)\n",
                                        next_level_paddr,
                                        p);
                                res = -E_RENDEZVOS;
                                goto map_l2_fail;

                        } else {
                                /* Case 1 & 2: Invalid entry with non-zero
                                 * physical address */
                                /* This indicates data corruption or an
                                 * uninitialized page table entry */
                                if (next_level_paddr != 0) {
                                        pr_error(
                                                "[ MAP ] invalid entry with non-zero physical address 0x%lx (possible data corruption)\n",
                                                next_level_paddr);
                                        res = -E_RENDEZVOS;
                                        goto map_l2_fail;
                                }
                                /* next_level_paddr == 0: truly empty entry,
                                 * should not reach here */
                                pr_error(
                                        "[ MAP ] unexpected empty entry in != branch\n");
                                res = -E_RENDEZVOS;
                                goto map_l2_fail;
                        }
                }

                /*
                 * Remap same physical page: update flags (mprotect scenario).
                 * First verify that the existing entry is valid.
                 */
                pt_entry =
                        ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)]
                                .entry;
                entry_flags = arch_encode_flags(2, pt_entry);

                if (!(entry_flags & PAGE_ENTRY_VALID)) {
                        /* Entry is invalid but points to same physical page -
                         * data corruption */
                        pr_error(
                                "[ MAP ] cannot update flags: invalid entry with physical address 0x%lx (data corruption?)\n",
                                next_level_paddr);
                        res = -E_RENDEZVOS;
                        goto map_l2_fail;
                }

                /* Valid entry with same physical page - safe to update flags
                 * (mprotect) */
                pr_debug(
                        "[ MAP ] remapping same 2M physical page with updated flags (addr: 0x%lx, old_flags: 0x%lx, new_flags: 0x%lx)\n",
                        v,
                        entry_flags,
                        flags);
                arch_set_L2_entry(
                        p, v, (union L2_entry *)(handler->map_vaddr[2]), flags);
                goto map_succ;
        } else {
                next_level_paddr = L2_entry_addr(((
                        union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)]);
                if (!next_level_paddr) {
                        /*no next level page, need alloc one*/
                        if (invalid_ppn(handler->handler_ppn[3])) {
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
        /*map the L2 table to one L3 table entry*/
        pt_entry =
                ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)].entry;
        entry_flags = arch_encode_flags(2, pt_entry);
        if (entry_flags & PAGE_ENTRY_HUGE) {
                pr_error(
                        "[ MAP ] try to map a level 3 page but a level 2 page have mapped here\n");
                res = -E_RENDEZVOS;
                goto map_l2_fail;
        }
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
        pt_entry =
                ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)].entry;
        entry_flags = arch_encode_flags(3, pt_entry);
        if (!(entry_flags & PAGE_ENTRY_VALID)) {
                pr_error(
                        "[ MAP ] cannot modify L3 leaf: invalid entry (phys 0x%lx, vaddr 0x%lx)\n",
                        next_level_paddr,
                        v);
                res = -E_RENDEZVOS;
                goto map_l3_fail;
        }

        flags = arch_decode_flags(3, eflags);
        if (next_level_paddr != p && !allow_remap) {
                /* Different physical page and not allowed 4K remap */
                pr_error(
                        "[ MAP ] cannot map 4K page: different physical page already mapped (old: 0x%lx, new: 0x%lx, vaddr: 0x%lx)\n",
                        next_level_paddr,
                        p,
                        v);
                res = -E_RENDEZVOS;
                goto map_l3_fail;
        } else {
                pr_debug(
                        "[ MAP ] remapping same 4K physical page with updated flags (vaddr: 0x%lx, old_flags: 0x%lx, new_flags: 0x%lx)\n",
                        v,
                        entry_flags,
                        flags);
        }
        arch_set_L3_entry(
                p, v, (union L3_entry *)(handler->map_vaddr[3]), flags);
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
        unlock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
map_fail:
        /*
         * Replenish handler_ppn[] slots consumed during this map attempt. We do
         * not roll back already-installed page-table pages here: kernel page
         * table growth is bounded by design and parameters; user page tables
         * are torn down on process exit. Unbounded growth would indicate
         * leakage or misuse above this layer, not something to fix by rollback.
         */
        struct pmm *pmm_ptr = handler->pmm;
        for (int i = 0; i < 4; i++) {
                if (handler->handler_ppn[i] == -E_RENDEZVOS) {
                        handler->handler_ppn[i] = pmm_ptr->pmm_alloc(
                                pmm_ptr, 1, &alloced_page_number);
                }
        }
        return res;
}

ppn_t unmap(VS_Common *vs, vpn_t vpn, u64 new_entry_addr,
            struct map_handler *handler)
{
        if (!vs || !handler) {
                return 0;
        }
        if (!vs_common_is_table_vspace(vs)) {
                pr_error(
                        "[ MAP ] ERROR: unmap called with KERNEL_HEAP_REF vs\n");
                return -E_IN_PARAM;
        }
        lock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
        vaddr v = VADDR(vpn);
        paddr next_level_paddr = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        union L0_entry L0_E;
        union L1_entry L1_E;
        union L2_entry L2_E;
        union L3_entry L3_E;
        ppn_t ppn = 0;
        if (!vs->vspace_root_addr || !vpn) {
                pr_error("[ ERROR ] unmap input is not right\n");
                ppn = -E_IN_PARAM;
                goto unmap_fail;
        } else if (ROUND_DOWN(vs->vspace_root_addr, PAGE_SIZE)
                   != vs->vspace_root_addr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr 0x%lx in mapping, please check\n",
                        vs->vspace_root_addr);
                ppn = -E_IN_PARAM;
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
                        "[ ERROR ] L0 entry not mapped, entry is 0x%lx, unmap error\n",
                        L0_E);
                ppn = -E_RENDEZVOS;
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
                ppn = -E_RENDEZVOS;
                goto unmap_l1_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                pr_error("[ ERROR ] we do not use 1G huge page, unmap error\n");
                ppn = -E_RENDEZVOS;
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
                pr_error("[ ERROR ] L2 entry %lx not mapped, unmap error\n",
                         L2_E.entry);
                ppn = -E_RENDEZVOS;
                goto unmap_l2_fail;
        } else if (is_final_level_pt(2, entry_flags)) {
                /*check the vpn 2M aligned*/
                if (vpn & mask_9_bit) {
                        pr_error(
                                "[ ERROR ] try to unmap a 2M entry %lx %lx %lx %d, but vpn is not 2M  aligned\n",
                                L2_E.entry,
                                entry_flags,
                                v,
                                per_cpu(cpu_number, handler->cpu_id));
                        ppn = -E_RENDEZVOS;
                        goto unmap_l2_fail;
                }
                ppn = PPN(next_level_paddr);
                ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)].entry =
                        0;
                ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)].paddr =
                        new_entry_addr;
                goto unmap_succ;
        }

        /*=== === === L3 table === === ===*/
        util_map(next_level_paddr, handler->map_vaddr[3]);
        L3_E = ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)];
        entry_flags = arch_encode_flags(3, (ARCH_PFLAGS_t)L3_E.entry);
        next_level_paddr = L3_entry_addr(L3_E);
        // if (!next_level_paddr || !(entry_flags & PAGE_ENTRY_VALID)) /*we will
        //                                                                not
        //                                                                swap
        //                                                                out
        //                                                                the l3
        //                                                                page*/
        // {
        //         pr_error("[ ERROR ] L3 entry not mapped, unmap error\n");
        //         res = -E_RENDEZVOS;
        //         goto unmap_l3_fail;
        // }
        if (next_level_paddr && (entry_flags & PAGE_ENTRY_VALID)) {
                ppn = PPN(next_level_paddr);
        }
        ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)].entry = 0;
        ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)].paddr =
                new_entry_addr;
unmap_succ:
        arch_tlb_invalidate_page(vs->vspace_id, v);
        // unmap_l3_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[3]);
unmap_l2_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[2]);
unmap_l1_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
unmap_l0_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
unmap_fail:
        unlock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
        return ppn;
}
ppn_t have_mapped(VS_Common *vs, vpn_t vpn, ENTRY_FLAGS_t *entry_flags_out,
                  int *entry_level_out, struct map_handler *handler)
{
        vaddr v = VADDR(vpn);
        ppn_t ppn = 0;
        ENTRY_FLAGS_t entry_flags = 0;
        int entry_level = 0;
        union L0_entry L0_E;
        union L1_entry L1_E;
        union L2_entry L2_E;
        union L3_entry L3_E;
        if (entry_flags_out)
                *entry_flags_out = 0;
        if (entry_level_out)
                *entry_level_out = 0;
        if (!vs || !vs->vspace_root_addr || !vpn || !handler) {
                pr_error("[ ERROR ] check input is not right\n");
                ppn = -E_IN_PARAM;
                goto have_mapped_fail;
        } else if (ROUND_DOWN(vs->vspace_root_addr, PAGE_SIZE)
                   != vs->vspace_root_addr) {
                pr_error(
                        "[ ERROR ] wrong vspace root paddr 0x%lx in mapping, please check\n",
                        vs->vspace_root_addr);
                ppn = -E_RENDEZVOS;
                goto have_mapped_fail;
        }
        if (!vs_common_is_table_vspace(vs)) {
                pr_error(
                        "[ MAP ] ERROR: have_mapped called with KERNEL_HEAP_REF vs\n");
                return -E_IN_PARAM;
        }
        lock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
        /*=== === === L0 table === === ===*/
        util_map(vs->vspace_root_addr, handler->map_vaddr[0]);
        L0_E = ((union L0_entry *)(handler->map_vaddr[0]))[L0_INDEX(v)];
        entry_flags = arch_encode_flags(0, (ARCH_PFLAGS_t)L0_E.entry);
        ppn = L0_E.paddr;
        if (!ppn || !(entry_flags & PAGE_ENTRY_VALID)) {
                ppn = 0;
                goto have_mapped_l0_fail;
        }

        /*=== === === L1 table === === ===*/
        util_map(PADDR(ppn), handler->map_vaddr[1]);
        L1_E = ((union L1_entry *)(handler->map_vaddr[1]))[L1_INDEX(v)];
        entry_flags = arch_encode_flags(1, (ARCH_PFLAGS_t)L1_E.entry);
        ppn = L1_E.paddr;
        if (!ppn || !(entry_flags & PAGE_ENTRY_VALID)) {
                ppn = 0;
                goto have_mapped_l1_fail;
        } else if (is_final_level_pt(1, entry_flags)) {
                pr_error("[ ERROR ] we do not use 1G huge page, check error\n");
                ppn = 0;
                goto have_mapped_l1_fail;
        }

        /*=== === === L2 table === === ===*/
        util_map(PADDR(ppn), handler->map_vaddr[2]);
        L2_E = ((union L2_entry *)(handler->map_vaddr[2]))[L2_INDEX(v)];
        entry_flags = arch_encode_flags(2, (ARCH_PFLAGS_t)L2_E.entry);
        ppn = L2_E.paddr;
        if (!ppn || !(entry_flags & PAGE_ENTRY_VALID)) {
                ppn = 0;
                goto have_mapped_l2_fail;
        } else if (is_final_level_pt(2, entry_flags)) {
                entry_level = 2;
                goto have_mapped_succ;
        }

        /*=== === === L3 table === === ===*/
        util_map(PADDR(ppn), handler->map_vaddr[3]);
        L3_E = ((union L3_entry *)(handler->map_vaddr[3]))[L3_INDEX(v)];
        entry_flags = arch_encode_flags(3, (ARCH_PFLAGS_t)L3_E.entry);
        entry_level = 3;
        ppn = L3_E.paddr;
        if (!ppn || !(entry_flags & PAGE_ENTRY_VALID)) {
                ppn = 0;
        }
have_mapped_succ:
        if (ppn > 0) {
                if (entry_flags_out)
                        *entry_flags_out = entry_flags;
                if (entry_level_out)
                        *entry_level_out = entry_level;
        }
        arch_tlb_invalidate_page(vs->vspace_id, v);
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[3]);
have_mapped_l2_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[2]);
have_mapped_l1_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
have_mapped_l0_fail:
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
        unlock_mcs(&vs->vspace_lock, &handler->vspace_lock_node);
have_mapped_fail:
        return ppn;
}
paddr new_vs_root(paddr old_vs_root_paddr, struct map_handler *handler)
{
        /*no need to lock here*/
        paddr vs_root = 0;
        size_t alloced_page_number;
        if (!handler || !handler->pmm || invalid_ppn(handler->handler_ppn[0])) {
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
         * Here we only provide an interface for copy user part L0 entry from
         * old_vs_root_paddr, but COW might use a deep(I mean build a new page
         * table tree, and only share L3 pte) In that case this interface should
         * not use.
         */
        if (old_vs_root_paddr) {
                util_map(old_vs_root_paddr, handler->map_vaddr[1]);
                memcpy((void *)(handler->map_vaddr[0]),
                       (void *)(handler->map_vaddr[1]),
                       PAGE_SIZE / 2);
                arch_tlb_invalidate_page(0, handler->map_vaddr[1]);
        } else {
                memset((void *)(handler->map_vaddr[0]), 0, PAGE_SIZE / 2);
        }

        arch_tlb_invalidate_page(0, handler->map_vaddr[0]);

        if (handler->handler_ppn[0] == -E_RENDEZVOS) {
                struct pmm *pmm_ptr = handler->pmm;
                handler->handler_ppn[0] =
                        pmm_ptr->pmm_alloc(pmm_ptr, 1, &alloced_page_number);
        }
new_vs_root_fail:
        return vs_root;
}

error_t vspace_free_user_pt(VS_Common *vs, struct map_handler *handler)
{
        if (!handler || !handler->pmm || !vs->vspace_root_addr)
                return -E_IN_PARAM;

        const u32 pt_entries_per_table = (u32)(PAGE_SIZE / PTE_SIZE);

        util_map(vs->vspace_root_addr, handler->map_vaddr[0]);
        union L0_entry *l0_table = (union L0_entry *)handler->map_vaddr[0];

        bool root_nonempty = false;

        /* User canonical space: L0 scans only the lower half. */
        for (u32 l0_index = 0; l0_index < pt_entries_per_table / 2;
             l0_index++) {
                ENTRY_FLAGS_t l0_flags = arch_encode_flags(
                        0, (ARCH_PFLAGS_t)l0_table[l0_index].entry);
                if (!(l0_flags & PAGE_ENTRY_VALID))
                        continue;

                paddr l1_table_paddr = L0_entry_addr(l0_table[l0_index]);
                util_map(l1_table_paddr, handler->map_vaddr[1]);
                union L1_entry *l1_table =
                        (union L1_entry *)handler->map_vaddr[1];

                bool l1_nonempty = false;

                for (u32 l1_index = 0; l1_index < pt_entries_per_table;
                     l1_index++) {
                        ENTRY_FLAGS_t l1_flags = arch_encode_flags(
                                1, (ARCH_PFLAGS_t)l1_table[l1_index].entry);
                        if (!(l1_flags & PAGE_ENTRY_VALID))
                                continue;

                        /* Unsupported huge/final-level: keep this path. */
                        if (is_final_level_pt(1, l1_flags)) {
                                root_nonempty = true;
                                l1_nonempty = true;
                                continue;
                        }

                        paddr l2_table_paddr =
                                L1_entry_addr(l1_table[l1_index]);
                        util_map(l2_table_paddr, handler->map_vaddr[2]);
                        union L2_entry *l2_table =
                                (union L2_entry *)handler->map_vaddr[2];

                        bool l2_nonempty = false;

                        for (u32 l2_index = 0; l2_index < pt_entries_per_table;
                             l2_index++) {
                                ENTRY_FLAGS_t l2_flags = arch_encode_flags(
                                        2,
                                        (ARCH_PFLAGS_t)l2_table[l2_index].entry);
                                if (!(l2_flags & PAGE_ENTRY_VALID))
                                        continue;

                                /* Unsupported huge/final-level: keep this path.
                                 */
                                if (is_final_level_pt(2, l2_flags)) {
                                        root_nonempty = true;
                                        l2_nonempty = true;
                                        continue;
                                }

                                paddr l3_table_paddr =
                                        L2_entry_addr(l2_table[l2_index]);
                                util_map(l3_table_paddr, handler->map_vaddr[3]);
                                union L3_entry *l3_table =
                                        (union L3_entry *)handler->map_vaddr[3];

                                bool l3_nonempty = false;
                                for (u32 l3_index = 0;
                                     l3_index < pt_entries_per_table;
                                     l3_index++) {
                                        ENTRY_FLAGS_t l3_flags =
                                                arch_encode_flags(
                                                        3,
                                                        (ARCH_PFLAGS_t)
                                                                l3_table[l3_index]
                                                                        .entry);
                                        if (l3_flags & PAGE_ENTRY_VALID) {
                                                l3_nonempty = true;
                                                break;
                                        }
                                        /*no need to clean the va, for all the
                                         * page must be unmapped and cleaned
                                         * tlb*/
                                }

                                if (l3_nonempty) {
                                        root_nonempty = true;
                                        l2_nonempty = true;
                                } else {
                                        /* Empty L3 => free it, clear parent
                                         * entry. */
                                        (void)handler->pmm->pmm_free(
                                                handler->pmm,
                                                PPN(l3_table_paddr),
                                                1);
                                        l2_table[l2_index].entry = 0;
                                        l2_table[l2_index].paddr = 0;
                                }
                                arch_tlb_invalidate_page(vs->vspace_id,
                                                         handler->map_vaddr[3]);
                        }

                        if (!l2_nonempty) {
                                /* All child L3 were empty => free L2. */
                                (void)handler->pmm->pmm_free(
                                        handler->pmm, PPN(l2_table_paddr), 1);
                                l1_table[l1_index].entry = 0;
                                l1_table[l1_index].paddr = 0;
                        } else {
                                l1_nonempty = true;
                        }
                        arch_tlb_invalidate_page(vs->vspace_id,
                                                 handler->map_vaddr[2]);
                }

                if (!l1_nonempty) {
                        /* All child L2 were empty => free L1. */
                        (void)handler->pmm->pmm_free(
                                handler->pmm, PPN(l1_table_paddr), 1);
                        l0_table[l0_index].entry = 0;
                        l0_table[l0_index].paddr = 0;
                }
                arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[1]);
        }

        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
        return root_nonempty ? -E_RENDEZVOS : REND_SUCCESS;
}

error_t vspace_free_root_page(VS_Common *vs, struct map_handler *handler)
{
        if (!handler || !handler->pmm || !vs->vspace_root_addr)
                return -E_IN_PARAM;
        (void)handler->pmm->pmm_free(
                handler->pmm, PPN(vs->vspace_root_addr), 1);
        vs->vspace_root_addr = 0;
        arch_tlb_invalidate_page(vs->vspace_id, handler->map_vaddr[0]);
        return REND_SUCCESS;
}
