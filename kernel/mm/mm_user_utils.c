/*
 * User VA-range templates: radix + map + PMM (see mm_user_utils.h).
 */
#include <common/align.h>
#include <common/limits.h>
#include <common/string.h>
#include <common/taggedptr.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/mm_user_utils.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/vmm_radix_tree.h>
#include <rendezvos/smp/percpu.h>

/** PTE/radix flags for @c map(): drop SW bits; keep VALID if @p in had it. */
static ENTRY_FLAGS_t mm_user_canonical_pte_flags(ENTRY_FLAGS_t in)
{
        ENTRY_FLAGS_t out = entry_flags_rm_sw_flags(in);
        if (in & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID)
                out |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;
        return out;
}

/**
 * @p for_pte_table true: L3 PTE flags; false: radix leaf shadow.
 * @c MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY with radix: returns @p old_flags unchanged.
 */
static ENTRY_FLAGS_t
mm_user_updated_entry_flags(mm_user_range_flags_mode_t mode, bool for_pte_table,
                            ENTRY_FLAGS_t old_flags, ENTRY_FLAGS_t set_mask,
                            ENTRY_FLAGS_t clear_mask)
{
        if (mode == MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY && !for_pte_table)
                return old_flags;
        u64 old_u = (u64)old_flags;
        u64 desired_u;
        if (mode == MM_USER_RANGE_FLAGS_ABSOLUTE)
                desired_u = (u64)entry_flags_rm_sw_flags(set_mask);
        else
                desired_u = (old_u | (u64)set_mask) & ~(u64)clear_mask;
        ENTRY_FLAGS_t out = (ENTRY_FLAGS_t)desired_u;
        out = entry_flags_rm_sw_flags(out);
        if (old_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID)
                out |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;
        return out;
}

/** Re-@c map prefix pages after a failed @ref mm_user_utils_set_range_flags PTE pass. */
static void mm_user_restore_range_pte_prefix(struct VSpace* vs, vaddr range_start,
                                             size_t pages_mapped,
                                             ENTRY_FLAGS_t canonical_pte_flags,
                                             struct map_handler* handler)
{
        ENTRY_FLAGS_t map_pte = mm_user_canonical_pte_flags(canonical_pte_flags);

        for (size_t page_index = 0; page_index < pages_mapped; page_index++) {
                vaddr page_va =
                        range_start + (vaddr)page_index * (vaddr)PAGE_SIZE;
                int pte_level = 3;
                ENTRY_FLAGS_t pte_flags = 0;
                ppn_t mapped_ppn = have_mapped(
                        vs, VPN(page_va), &pte_flags, &pte_level, handler);
                if ((i64)mapped_ppn < 0 || invalid_ppn(mapped_ppn)
                    || pte_level != 3) {
                        continue;
                }
                (void)map(vs,
                          mapped_ppn,
                          VPN(page_va),
                          pte_level,
                          map_pte,
                          handler);
        }
}

vaddr mm_user_utils_set_range_and_fill(struct VSpace* vs, vaddr range_start,
                                       size_t page_count, ENTRY_FLAGS_t flags)
{
        struct map_handler* handler = &percpu(Map_Handler);
        if (!vs || vs == &root_vspace || !vs->pmm || page_count == 0
            || ROUND_DOWN(range_start, PAGE_SIZE) != range_start
            || !vs->vspace_root_addr || !vs->root_radix) {
                return 0;
        }

        VSpace* owner_vs = vs->root_vs ? vs->root_vs : vs;
        tagged_ptr_t owner = tp_new((void*)owner_vs, (u16)percpu(cpu_number));

        ENTRY_FLAGS_t caller_flags = flags;
        if (caller_flags == PAGE_ENTRY_NONE)
                caller_flags = 0;
        ENTRY_FLAGS_t bind_flags = entry_flags_rm_sw_flags(caller_flags);
        if (!(bind_flags & PAGE_ENTRY_VALID))
                bind_flags |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;

        struct pmm* pmm = vs->pmm;
        size_t pages_allocated = 0;
        ppn_t ppn_first = pmm->pmm_alloc(pmm, page_count, &pages_allocated);
        if (invalid_ppn(ppn_first)) {
                pr_error("[MM_USER] set_range_and_fill: pmm_alloc failed\n");
                return 0;
        } else if (pages_allocated < page_count) {
                if (pages_allocated > 0)
                        pmm->pmm_free(pmm, ppn_first, pages_allocated);
                pr_error("[MM_USER] set_range_and_fill: pmm_alloc short alloc\n");
                return 0;
        } else if (pages_allocated > page_count) {
                pmm->pmm_free(pmm,
                              ppn_first + (ppn_t)page_count,
                              pages_allocated - page_count);
        }

        vaddr range_end;
        error_t err;
        size_t pages_mapped = 0;
        bool radix_range_inserted = false;

        if (!vmm_radix_tree_calculate_end_check(
                    range_start, page_count, &range_end)) {
                pr_error(
                        "[MM_USER] set_range_and_fill: radix end check failed\n");
                goto out_free_phys;
        }
        err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler, (VSpace*)vs, range_start, range_end, RADIX_RL_INSERT);
        if (err != REND_SUCCESS) {
                pr_error("[MM_USER] set_range_and_fill: radix lock fail e=%d\n",
                         (int)err);
                goto out_free_phys;
        }
        if (vmm_radix_tree_insert_range(
                    (VSpace*)vs, owner, range_start, bind_flags, range_end)
            != REND_SUCCESS) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, range_start, range_end);
                pr_error("[MM_USER] set_range_and_fill: insert_range failed\n");
                goto out_delete_radix;
        }
        radix_range_inserted = true;
        while (pages_mapped < page_count) {
                vaddr page_va = range_start + pages_mapped * PAGE_SIZE;
                err = map(vs,
                          ppn_first + (ppn_t)pages_mapped,
                          VPN(page_va),
                          3,
                          bind_flags,
                          handler);
                if (err != REND_SUCCESS) {
                        pr_error(
                                "[MM_USER] set_range_and_fill: map fail page %lu e=%d\n",
                                (unsigned long)pages_mapped,
                                (int)err);
                        (void)vmm_radix_tree_unlock_range_small(
                                (VSpace*)vs, range_start, range_end);
                        goto out_unmap_pte_prefix;
                }
                pages_mapped++;
        }
        if (vmm_radix_tree_leaf_bind_range(
                    (VSpace*)vs, range_start, ppn_first, range_end, bind_flags)
            != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] set_range_and_fill: leaf_bind_range failed\n");
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, range_start, range_end);
                goto out_unmap_pte_prefix;
        }
        (void)vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);

        memset((void*)range_start, 0, page_count * PAGE_SIZE);

        return range_start;

out_unmap_pte_prefix:
        while (pages_mapped > 0) {
                pages_mapped--;
                vaddr page_va = range_start + pages_mapped * PAGE_SIZE;
                (void)unmap(vs, VPN(page_va), 0, handler);
        }
out_delete_radix:
        if (radix_range_inserted
            && vmm_radix_tree_calculate_end_check(
                    range_start, page_count, &range_end)
            && vmm_radix_tree_lock_range_small_with_big_locked(handler,
                                                               (VSpace*)vs,
                                                               range_start,
                                                               range_end,
                                                               RADIX_RL_DELETE)
                       == REND_SUCCESS) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, range_start, range_end);
        }
out_free_phys:
        pmm->pmm_free(pmm, ppn_first, page_count);
        return 0;
}

error_t mm_user_utils_fill_page_with_exist_range(struct VSpace* vs,
                                                 vaddr page_va,
                                                 ENTRY_FLAGS_t leaf_flags)
{
        struct map_handler* handler = &percpu(Map_Handler);
        error_t err;
        vaddr page_range_end;
        ppn_t new_ppn;

        if (!vs || vs == &root_vspace || !vs->pmm || !vs->vspace_root_addr
            || !vs->root_radix) {
                return -E_IN_PARAM;
        }
        if (ROUND_DOWN(page_va, PAGE_SIZE) != page_va)
                return -E_IN_PARAM;
        if (!vmm_radix_tree_calculate_end_check(page_va, 1, &page_range_end))
                return -E_IN_PARAM;

        err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler,
                (VSpace*)vs,
                page_va,
                page_range_end,
                RADIX_RL_QUERY_OR_CHANGE);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: lock fail e=%d\n",
                        (int)err);
                return err;
        }

        ENTRY_FLAGS_t radix_flags = 0;
        err = vmm_radix_tree_query_range((VSpace*)vs,
                                        page_va,
                                        page_range_end,
                                        &radix_flags,
                                        NULL);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: query_range fail e=%d\n",
                        (int)err);
                goto out_unlock_l2;
        }
        if (radix_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID) {
                return vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
        }
        if (!(radix_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_LAZY)) {
                err = -E_IN_PARAM;
                goto out_unlock_l2;
        }

        ENTRY_FLAGS_t bind_flags = entry_flags_rm_sw_flags(leaf_flags);
        if (!(bind_flags & PAGE_ENTRY_VALID))
                bind_flags |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;

        size_t pages_allocated = 0;
        new_ppn = vs->pmm->pmm_alloc(vs->pmm, 1, &pages_allocated);
        if (invalid_ppn(new_ppn)) {
                err = -E_RENDEZVOS;
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: pmm_alloc failed\n");
                goto out_unlock_l2;
        } else if (pages_allocated < 1) {
                err = -E_RENDEZVOS;
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: pmm_alloc short alloc\n");
                goto out_unlock_l2;
        } else if (pages_allocated > 1) {
                (void)vs->pmm->pmm_free(vs->pmm,
                                        new_ppn + 1,
                                        pages_allocated - 1);
        }

        err = map(vs, new_ppn, VPN(page_va), 3, bind_flags, handler);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: map fail e=%d\n",
                        (int)err);
                goto out_free_phys;
        }

        err = vmm_radix_tree_leaf_bind(
                (VSpace*)vs, page_va, new_ppn, bind_flags);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] fill_page_with_exist_range: leaf_bind fail e=%d\n",
                        (int)err);
                goto out_unmap_user_pte;
        }

        err = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_va, page_range_end);
        if (err != REND_SUCCESS)
                return err;
        memset((void*)page_va, 0, PAGE_SIZE);
        return REND_SUCCESS;

out_unmap_user_pte:
        (void)unmap(vs, VPN(page_va), 0, handler);
out_free_phys:
        (void)vs->pmm->pmm_free(vs->pmm, new_ppn, 1);
out_unlock_l2:
        (void)vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_va, page_range_end);
        return err;
}

error_t mm_user_utils_clean_range_and_unfill(struct VSpace* vs,
                                             vaddr range_start,
                                             size_t page_count, ppn_t ppn_first)
{
        struct map_handler* handler = &percpu(Map_Handler);
        if (!vs || !vs->pmm || page_count == 0
            || ROUND_DOWN(range_start, PAGE_SIZE) != range_start
            || invalid_ppn(ppn_first)) {
                return -E_IN_PARAM;
        }

        error_t err;
        vaddr range_end;
        if (!vmm_radix_tree_calculate_end_check(
                    range_start, page_count, &range_end)) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: radix end check fail\n");
                return -E_IN_PARAM;
        }

        err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler,
                (VSpace*)vs,
                range_start,
                range_end,
                RADIX_RL_QUERY_OR_CHANGE);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: QUERY lock fail va=%lx e=%d\n",
                        (unsigned long)range_start,
                        (int)err);
                goto out_fail_hold_l0;
        }
        err = vmm_radix_tree_leaf_unbind_range(
                (VSpace*)vs, range_start, ppn_first, range_end);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: leaf_unbind_range fail va=%lx e=%d\n",
                        (unsigned long)range_start,
                        (int)err);
                goto out_unlock_l2;
        }
        err = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: unlock after unbind fail va=%lx e=%d\n",
                        (unsigned long)range_start,
                        (int)err);
                goto out_fail_hold_l0;
        }

        for (size_t page_index = 0; page_index < page_count; page_index++) {
                vaddr page_va = range_start + page_index * PAGE_SIZE;
                ppn_t unmapped_ppn = unmap(vs, VPN(page_va), 0, handler);
                if (invalid_ppn(unmapped_ppn)) {
                        pr_error(
                                "[MM_USER] clean_range_and_unfill: unmap fail page %lu ppn=%ld\n",
                                (unsigned long)page_index,
                                (long)unmapped_ppn);
                        err = -E_RENDEZVOS;
                }
        }
        if (err != REND_SUCCESS) {
                goto out_fail_hold_l0;
        }

        err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler, (VSpace*)vs, range_start, range_end, RADIX_RL_DELETE);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: DELETE lock fail va=%lx e=%d\n",
                        (unsigned long)range_start,
                        (int)err);
                goto out_fail_hold_l0;
        }
        err = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: unlock after DELETE fail va=%lx e=%d\n",
                        (unsigned long)range_start,
                        (int)err);
                return err;
        }

        err = vs->pmm->pmm_free(vs->pmm, ppn_first, page_count);
        if (err != REND_SUCCESS) {
                pr_error(
                        "[MM_USER] clean_range_and_unfill: pmm_free fail va=%lx ppn=%ld count=%lu e=%d\n",
                        (unsigned long)range_start,
                        (long)ppn_first,
                        (unsigned long)page_count,
                        (int)err);
        }
        return err;

out_unlock_l2:
        (void)vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);
out_fail_hold_l0:
        return err;
}

error_t mm_user_utils_remap_page(struct VSpace* vs, vaddr page_va,
                                 ppn_t new_ppn, ENTRY_FLAGS_t new_flags,
                                 ppn_t expect_old_ppn)
{
        struct map_handler* handler = &percpu(Map_Handler);
        page_va = ROUND_DOWN(page_va, PAGE_SIZE);
        if (!vs || vs == &root_vspace || !vs->pmm || !vs->pmm->zone
            || !vs->root_radix || !vs->vspace_root_addr
            || invalid_ppn(new_ppn)) {
                return -E_IN_PARAM;
        }

        vaddr page_range_end;
        if (!vmm_radix_tree_calculate_end_check(page_va, 1, &page_range_end))
                return -E_IN_PARAM;

        error_t err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler,
                (VSpace*)vs,
                page_va,
                page_range_end,
                RADIX_RL_QUERY_OR_CHANGE);
        if (err != REND_SUCCESS)
                return err;

        ENTRY_FLAGS_t radix_flags = 0;
        err = vmm_radix_tree_query_range((VSpace*)vs,
                                        page_va,
                                        page_range_end,
                                        &radix_flags,
                                        NULL);
        if (err != REND_SUCCESS) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return err;
        }
        if (!(radix_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID)) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return -E_IN_PARAM;
        }

        int pte_level = 3;
        ppn_t old_ppn =
                have_mapped(vs, VPN(page_va), NULL, &pte_level, handler);
        if ((i64)old_ppn < 0) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return (error_t)old_ppn;
        } else if (invalid_ppn(old_ppn)) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return -E_REND_NOFOUND;
        } else if (!invalid_ppn(expect_old_ppn) && old_ppn != expect_old_ppn) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return -E_REND_RC_UNEQUAL;
        }

        ENTRY_FLAGS_t new_leaf_flags = entry_flags_rm_sw_flags(new_flags);
        if (!(new_leaf_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID))
                new_leaf_flags |= (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;

        /* Radix/rmap first; PTE via map (REMAP only when PPN changes). */
        err = vmm_radix_tree_change_leaf_ppn((VSpace*)vs,
                                             page_va,
                                             page_range_end,
                                             old_ppn,
                                             new_ppn,
                                             new_leaf_flags);
        if (err != REND_SUCCESS) {
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return err;
        }

        ENTRY_FLAGS_t map_flags = new_leaf_flags;
        if (old_ppn != new_ppn)
                map_flags |= (ENTRY_FLAGS_t)PAGE_ENTRY_REMAP;

        err = map(vs,
                  new_ppn,
                  VPN(page_va),
                  pte_level,
                  map_flags,
                  handler);
        if (err != REND_SUCCESS) {
                (void)vmm_radix_tree_change_leaf_ppn((VSpace*)vs,
                                                     page_va,
                                                     page_range_end,
                                                     new_ppn,
                                                     old_ppn,
                                                     radix_flags);
                (void)vmm_radix_tree_unlock_range_small(
                        (VSpace*)vs, page_va, page_range_end);
                return err;
        }

        if (old_ppn != new_ppn) {
                err = vs->pmm->pmm_free(vs->pmm, old_ppn, 1);
                if (err != REND_SUCCESS) {
                        pr_error("[MM_USER] remap_page: pmm_free old ppn=%ld e=%d\n",
                                 (long)old_ppn,
                                 (int)err);
                }
        }

        error_t unlock_err = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, page_va, page_range_end);
        if (unlock_err != REND_SUCCESS) {
                pr_error("[MM_USER] remap_page: unlock e=%d\n",
                         (int)unlock_err);
                return unlock_err;
        }
        return err;
}

error_t mm_user_utils_set_range_flags(struct VSpace* vs, vaddr range_start,
                                      u64 length_bytes,
                                      mm_user_range_flags_mode_t mode,
                                      ENTRY_FLAGS_t set_mask,
                                      ENTRY_FLAGS_t clear_mask)
{
        struct map_handler* handler = &percpu(Map_Handler);
        range_start = ROUND_DOWN(range_start, PAGE_SIZE);
        if (!vs || vs == &root_vspace || !vs->pmm || !vs->root_radix
            || !vs->vspace_root_addr || length_bytes == 0) {
                return -E_IN_PARAM;
        }
        if (length_bytes > (1ULL << 40))
                return -E_IN_PARAM;

        const size_t n_pages =
                (size_t)(ROUND_UP(length_bytes, PAGE_SIZE) / PAGE_SIZE);

        vaddr range_end;
        if (!vmm_radix_tree_calculate_end_check(
                    range_start, n_pages, &range_end)) {
                return -E_IN_PARAM;
        }

        error_t err = REND_SUCCESS;
        err = vmm_radix_tree_lock_range_small_with_big_locked(
                handler,
                (VSpace*)vs,
                range_start,
                range_end,
                RADIX_RL_QUERY_OR_CHANGE);
        if (err != REND_SUCCESS)
                return err;

        ENTRY_FLAGS_t uniform_radix_flags = 0;
        ENTRY_FLAGS_t uniform_pte_flags = 0;

        for (size_t page_index = 0; page_index < n_pages; page_index++) {
                vaddr page_va =
                        range_start + (vaddr)page_index * (vaddr)PAGE_SIZE;
                vaddr page_range_end;
                if (!vmm_radix_tree_calculate_end_check(
                            page_va, 1, &page_range_end)) {
                        err = -E_IN_PARAM;
                        goto out_unlock_l2;
                }
                ENTRY_FLAGS_t radix_flags = 0;
                err = vmm_radix_tree_query_range((VSpace*)vs,
                                                page_va,
                                                page_range_end,
                                                &radix_flags,
                                                NULL);
                if (err != REND_SUCCESS)
                        goto out_unlock_l2;
                if (!(radix_flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID)) {
                        err = -E_IN_PARAM;
                        goto out_unlock_l2;
                }
                int pte_level = 3;
                ENTRY_FLAGS_t pte_flags = 0;
                ppn_t mapped_ppn = have_mapped(
                        vs, VPN(page_va), &pte_flags, &pte_level, handler);
                if ((i64)mapped_ppn < 0) {
                        err = (error_t)mapped_ppn;
                        goto out_unlock_l2;
                }
                if (invalid_ppn(mapped_ppn)) {
                        err = -E_REND_NOFOUND;
                        goto out_unlock_l2;
                }
                if (pte_level != 3) {
                        err = -E_IN_PARAM;
                        goto out_unlock_l2;
                }
                if (page_index == 0) {
                        uniform_radix_flags = radix_flags;
                        uniform_pte_flags = pte_flags;
                } else if (radix_flags != uniform_radix_flags
                           || mm_user_canonical_pte_flags(pte_flags)
                                      != mm_user_canonical_pte_flags(
                                              uniform_pte_flags)) {
                        err = -E_IN_PARAM;
                        goto out_unlock_l2;
                }
        }

        ENTRY_FLAGS_t desired_pte_flags = mm_user_updated_entry_flags(
                mode, true, uniform_pte_flags, set_mask, clear_mask);
        ENTRY_FLAGS_t desired_radix_flags = mm_user_updated_entry_flags(
                mode, false, uniform_radix_flags, set_mask, clear_mask);
        size_t pages_pte_updated = 0;

        for (size_t page_index = 0; page_index < n_pages; page_index++) {
                vaddr page_va =
                        range_start + (vaddr)page_index * (vaddr)PAGE_SIZE;
                int pte_level = 3;
                ENTRY_FLAGS_t pte_flags = 0;
                ppn_t mapped_ppn = have_mapped(
                        vs, VPN(page_va), &pte_flags, &pte_level, handler);
                if ((i64)mapped_ppn < 0) {
                        err = (error_t)mapped_ppn;
                        goto out_rollback_pte;
                }
                if (invalid_ppn(mapped_ppn) || pte_level != 3) {
                        err = -E_RENDEZVOS;
                        goto out_rollback_pte;
                }
                err = map(vs,
                          mapped_ppn,
                          VPN(page_va),
                          pte_level,
                          desired_pte_flags,
                          handler);
                if (err != REND_SUCCESS)
                        goto out_rollback_pte;
                pages_pte_updated++;
        }

        if (mode != MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY) {
                err = vmm_radix_tree_change_range_flag((VSpace*)vs,
                                                       range_start,
                                                       range_end,
                                                       desired_radix_flags);
                if (err != REND_SUCCESS)
                        goto out_rollback_pte;
        }

        err = vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);
        if (err != REND_SUCCESS) {
                pr_error("[MM_USER] set_range_flags: unlock e=%d\n", (int)err);
        }
        return err;

out_rollback_pte:
        mm_user_restore_range_pte_prefix(vs,
                                       range_start,
                                       pages_pte_updated,
                                       uniform_pte_flags,
                                       handler);
out_unlock_l2:
        (void)vmm_radix_tree_unlock_range_small(
                (VSpace*)vs, range_start, range_end);
        return err;
}
