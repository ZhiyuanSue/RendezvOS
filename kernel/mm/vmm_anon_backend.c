/*
 * Default user anonymous page backend (see mm_anon_backend.h).
 * Orchestrates radix + map + bind; does not own vspace lifecycle (vmm).
 *
 * Radix band protocol matches core/kernel/mm/kmalloc.c eager paths:
 * core_get_free_pages (INSERT → map → insert_bind) and core_free_pages
 * (QUERY_OR_CHANGE → leaf_unbind_range → unlock → unmap → DELETE → pmm_free).
 */
#include <common/align.h>
#include <common/taggedptr.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm_anon_backend.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/vmm_radix_tree.h>
#include <rendezvos/smp/percpu.h>

vaddr mm_anon_map_pages_eager(struct map_handler* handler, struct VSpace* vs,
                              vaddr uva, size_t page_num, ENTRY_FLAGS_t flags)
{
        if (!handler || !vs || vs == &root_vspace || !vs->pmm || page_num == 0
            || ROUND_DOWN(uva, PAGE_SIZE) != uva || !vs->vspace_root_addr
            || !vs->root_radix) {
                return 0;
        }

        VSpace* insert_root_vs = vs->root_vs ? vs->root_vs : vs;
        tagged_ptr_t owner_tp =
                tp_new((void*)insert_root_vs, (u16)percpu(cpu_number));

        ENTRY_FLAGS_t extra = flags;
        if (extra == PAGE_ENTRY_NONE)
                extra = 0;
        ENTRY_FLAGS_t leaf_eflags = entry_flags_rm_sw_flags(extra);
        if (!(leaf_eflags & PAGE_ENTRY_VALID))
                leaf_eflags |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;

        struct pmm* pmm_ptr = vs->pmm;
        size_t alloced_page_number = 0;
        ppn_t ppn = pmm_ptr->pmm_alloc(pmm_ptr, page_num, &alloced_page_number);
        if (invalid_ppn(ppn) || alloced_page_number != page_num) {
                if (!invalid_ppn(ppn) && alloced_page_number > page_num)
                        pmm_ptr->pmm_free(pmm_ptr, ppn, alloced_page_number);
                pr_error(
                        "[MM_ANON] mm_anon_map_pages_eager: pmm_alloc failed\n");
                return 0;
        }

        vaddr vaddr_end;
        error_t err_lock;
        size_t mapped_count = 0;

        if (!vmm_radix_tree_calculate_end_check(
                    uva, alloced_page_number, &vaddr_end)) {
                pr_error(
                        "[MM_ANON] mm_anon_map_pages_eager: radix end check failed\n");
                goto before_lock_fail;
        }
        err_lock = vmm_radix_tree_lock_range_small(
                handler, (VSpace*)vs, uva, vaddr_end, RADIX_RL_INSERT);
        if (err_lock != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_map_pages_eager: radix lock fail e=%d\n",
                        err_lock);
                goto before_lock_fail;
        }
        while (mapped_count < alloced_page_number) {
                vaddr va = uva + mapped_count * PAGE_SIZE;
                error_t map_err = map(vs,
                                      ppn + (ppn_t)mapped_count,
                                      VPN(va),
                                      3,
                                      leaf_eflags,
                                      handler);
                if (map_err != REND_SUCCESS) {
                        pr_error(
                                "[MM_ANON] mm_anon_map_pages_eager: map fail page %lu e=%d\n",
                                mapped_count,
                                map_err);
                        (void)vmm_radix_tree_unlock_range_small(
                                (VSpace*)vs, uva, vaddr_end);
                        goto fail_unmap_rollback;
                }
                mapped_count++;
        }
        if (vmm_radix_tree_insert_bind_range(
                    handler, vs, owner_tp, uva, leaf_eflags, vaddr_end, ppn)
            != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_map_pages_eager: insert_bind_range failed\n");
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, uva, vaddr_end);
                goto fail_unmap_rollback;
        }
        (void)vmm_radix_tree_unlock_range_small((VSpace*)vs, uva, vaddr_end);

        return uva;

fail_unmap_rollback:
        while (mapped_count > 0) {
                mapped_count--;
                vaddr va = uva + mapped_count * PAGE_SIZE;
                (void)unmap(vs, VPN(va), 0, handler);
        }
before_lock_fail:
        if (vmm_radix_tree_calculate_end_check(uva, alloced_page_number,
                                               &vaddr_end)
            && vmm_radix_tree_lock_range_small(
                       handler, (VSpace*)vs, uva, vaddr_end, RADIX_RL_DELETE)
                       == REND_SUCCESS)
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, uva, vaddr_end);
        pmm_ptr->pmm_free(pmm_ptr, ppn, alloced_page_number);
        return 0;
}

vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags)
{
        return mm_anon_map_pages_eager(
                &percpu(Map_Handler), vs, uva, page_num, flags);
}

error_t mm_anon_reserve_lazy_range(struct map_handler* handler,
                                   struct VSpace* vs, tagged_ptr_t owner,
                                   vaddr page_vaddr, size_t page_number,
                                   ENTRY_FLAGS_t lazy_flags)
{
        if (!handler || !vs || !vs->root_radix)
                return -E_IN_PARAM;
        vaddr vaddr_end;
        if (!vmm_radix_tree_calculate_end_check(
                    page_vaddr, page_number, &vaddr_end))
                return -E_IN_PARAM;
        error_t err_lock = vmm_radix_tree_lock_range_small(
                handler, (VSpace*)vs, page_vaddr, vaddr_end, RADIX_RL_INSERT);
        if (err_lock != REND_SUCCESS)
                return err_lock;
        error_t err_insert = vmm_radix_tree_insert_range(
                handler, vs, owner, page_vaddr, lazy_flags, vaddr_end);
        error_t err_unlock = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_vaddr, vaddr_end);
        if (err_insert != REND_SUCCESS)
                return err_insert;
        return err_unlock;
}

error_t mm_anon_zero_fill_fault_page(struct map_handler* handler,
                                     struct VSpace* vs, vaddr fault_page_va,
                                     ENTRY_FLAGS_t leaf_flags)
{
        (void)handler;
        (void)vs;
        (void)fault_page_va;
        (void)leaf_flags;
        /* Wired in linux_layer fault path after lazy radix policy is finalized
         */
        return -E_RENDEZVOS;
}

error_t mm_anon_unmap_release_range(struct map_handler* handler,
                                    struct VSpace* vs, vaddr page_vaddr,
                                    size_t page_number, ppn_t ppn_first)
{
        if (!handler || !vs || !vs->pmm || page_number == 0
            || ROUND_DOWN(page_vaddr, PAGE_SIZE) != page_vaddr
            || invalid_ppn(ppn_first)) {
                return -E_IN_PARAM;
        }

        vaddr vaddr_end;
        if (!vmm_radix_tree_calculate_end_check(
                    page_vaddr, page_number, &vaddr_end)) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: radix end check fail\n");
                return -E_IN_PARAM;
        }
        error_t err_lock =
                vmm_radix_tree_lock_range_small(handler,
                                                (VSpace*)vs,
                                                page_vaddr,
                                                vaddr_end,
                                                RADIX_RL_QUERY_OR_CHANGE);
        if (err_lock != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: QUERY_OR_CHANGE lock fail va=%lx e=%d\n",
                        (unsigned long)page_vaddr,
                        (int)err_lock);
                return err_lock;
        }
        error_t err_unbind = vmm_radix_tree_leaf_unbind_range(
                handler, vs, page_vaddr, ppn_first, vaddr_end);
        error_t err_unlock = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_vaddr, vaddr_end);
        if (err_unbind != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: leaf_unbind_range fail va=%lx e=%d\n",
                        (unsigned long)page_vaddr,
                        (int)err_unbind);
                return err_unbind;
        }
        if (err_unlock != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: unlock after unbind fail va=%lx e=%d\n",
                        (unsigned long)page_vaddr,
                        (int)err_unlock);
                return err_unlock;
        }

        for (size_t pg = 0; pg < page_number; pg++) {
                vaddr va = page_vaddr + pg * PAGE_SIZE;
                ppn_t u = unmap(vs, VPN(va), 0, handler);
                if (invalid_ppn(u)) {
                        pr_error(
                                "[MM_ANON] mm_anon_unmap_release_range: unmap fail page %lu ppn=%ld\n",
                                (unsigned long)pg,
                                (long)u);
                        if (u < 0)
                                return (error_t)u;
                        return -E_RENDEZVOS;
                }
        }

        err_lock = vmm_radix_tree_lock_range_small(
                handler, (VSpace*)vs, page_vaddr, vaddr_end, RADIX_RL_DELETE);
        if (err_lock != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: DELETE lock fail va=%lx e=%d\n",
                        (unsigned long)page_vaddr,
                        (int)err_lock);
                return err_lock;
        }
        err_unlock = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_vaddr, vaddr_end);
        if (err_unlock != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: unlock after DELETE fail va=%lx e=%d\n",
                        (unsigned long)page_vaddr,
                        (int)err_unlock);
                return err_unlock;
        }

        error_t err_free =
                vs->pmm->pmm_free(vs->pmm, ppn_first, page_number);
        if (err_free != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_unmap_release_range: pmm_free fail va=%lx ppn=%ld np=%lu e=%d\n",
                        (unsigned long)page_vaddr,
                        (long)ppn_first,
                        (unsigned long)page_number,
                        (int)err_free);
        }
        return err_free;
}

error_t mm_anon_query_radix_leaf(struct VSpace* vs, vaddr page_va,
                                 ENTRY_FLAGS_t* out_flags,
                                 tagged_ptr_t* out_owner)
{
        struct map_handler* h = &percpu(Map_Handler);
        vaddr vaddr_end;
        if (!vmm_radix_tree_calculate_end_check(page_va, 1, &vaddr_end))
                return -E_IN_PARAM;
        error_t err_lock = vmm_radix_tree_lock_range_small(
                h, (VSpace*)vs, page_va, vaddr_end, RADIX_RL_QUERY_OR_CHANGE);
        if (err_lock != REND_SUCCESS)
                return err_lock;
        error_t err_query = vmm_radix_tree_query_leaf(
                h, (VSpace*)vs, page_va, vaddr_end, out_flags, out_owner);
        (void)vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_va, vaddr_end);
        return err_query;
}

error_t mm_anon_cow_replace_leaf(struct map_handler* handler, struct VSpace* vs,
                                 vaddr page_va, ppn_t old_ppn, ppn_t new_ppn,
                                 ENTRY_FLAGS_t new_flags)
{
        if (!handler || !vs)
                return -E_IN_PARAM;
        vaddr vaddr_end;
        if (!vmm_radix_tree_calculate_end_check(page_va, 1, &vaddr_end))
                return -E_IN_PARAM;
        error_t err_lock =
                vmm_radix_tree_lock_range_small(handler,
                                                (VSpace*)vs,
                                                page_va,
                                                vaddr_end,
                                                RADIX_RL_QUERY_OR_CHANGE);
        if (err_lock != REND_SUCCESS)
                return err_lock;
        error_t err_change = vmm_radix_tree_change_leaf_ppn(
                handler, vs, page_va, vaddr_end, old_ppn, new_ppn, new_flags);
        error_t err_unlock = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_va, vaddr_end);
        if (err_change != REND_SUCCESS)
                return err_change;
        return err_unlock;
}
