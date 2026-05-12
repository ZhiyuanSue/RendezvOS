/*
 * Default user anonymous page backend (see mm_anon_backend.h).
 * Orchestrates radix + map + bind; does not own vspace lifecycle (vmm).
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
                pr_error("[MM_ANON] mm_anon_map_pages_eager: pmm_alloc failed\n");
                return 0;
        }

        size_t mapped_count = 0;
        while (mapped_count < alloced_page_number) {
                vaddr va = uva + mapped_count * PAGE_SIZE;
                error_t me = map(vs,
                                 ppn + (ppn_t)mapped_count,
                                 VPN(va),
                                 3,
                                 leaf_eflags,
                                 handler);
                if (me != REND_SUCCESS) {
                        pr_error(
                                "[MM_ANON] mm_anon_map_pages_eager: map fail page %lu\n",
                                (unsigned long)mapped_count);
                        goto fail_unmap_radix_pmm;
                }
                mapped_count++;
        }
        if (vmm_radix_tree_insert_bind_range(handler,
                                             vs,
                                             owner_tp,
                                             uva,
                                             leaf_eflags,
                                             alloced_page_number,
                                             ppn)
            != REND_SUCCESS) {
                pr_error(
                        "[MM_ANON] mm_anon_map_pages_eager: insert_bind_range failed\n");
                goto fail_unmap_radix_pmm;
        }

        return uva;

fail_unmap_radix_pmm:
        while (mapped_count > 0) {
                mapped_count--;
                vaddr va = uva + mapped_count * PAGE_SIZE;
                (void)unmap(vs, VPN(va), 0, handler);
        }
        (void)vmm_radix_tree_delete_range(
                handler, vs, uva, alloced_page_number);
        pmm_ptr->pmm_free(pmm_ptr, ppn, alloced_page_number);
        return 0;
}

vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags)
{
        return mm_anon_map_pages_eager(&percpu(Map_Handler), vs, uva, page_num,
                                       flags);
}

error_t mm_anon_reserve_lazy_range(struct map_handler* handler, struct VSpace* vs,
                                   tagged_ptr_t owner, vaddr page_vaddr,
                                   size_t page_number, ENTRY_FLAGS_t lazy_flags)
{
        if (!handler || !vs || !vs->root_radix)
                return -E_IN_PARAM;
        return vmm_radix_tree_insert_range(
                handler, vs, owner, page_vaddr, lazy_flags, page_number);
}

error_t mm_anon_zero_fill_fault_page(struct map_handler* handler,
                                     struct VSpace* vs, vaddr fault_page_va,
                                     ENTRY_FLAGS_t leaf_flags)
{
        (void)handler;
        (void)vs;
        (void)fault_page_va;
        (void)leaf_flags;
        /* Wired in linux_layer fault path after lazy radix policy is finalized */
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

        size_t i = page_number;
        while (i > 0) {
                i--;
                vaddr va = page_vaddr + i * PAGE_SIZE;
                (void)unmap(vs, VPN(va), 0, handler);
        }
        error_t u = vmm_radix_tree_leaf_unbind_range(
                handler, vs, page_vaddr, ppn_first, page_number);
        if (u != REND_SUCCESS)
                return u;
        error_t d = vmm_radix_tree_delete_range(handler, vs, page_vaddr,
                                                page_number);
        if (d != REND_SUCCESS)
                return d;
        vs->pmm->pmm_free(vs->pmm, ppn_first, page_number);
        return REND_SUCCESS;
}

error_t mm_anon_query_radix_leaf(struct VSpace* vs, vaddr page_va,
                                 ENTRY_FLAGS_t* out_flags,
                                 tagged_ptr_t* out_owner)
{
        return vmm_radix_tree_query_leaf(vs, page_va, out_flags, out_owner);
}

error_t mm_anon_cow_replace_leaf(struct map_handler* handler, struct VSpace* vs,
                                 vaddr page_va, ppn_t old_ppn, ppn_t new_ppn,
                                 ENTRY_FLAGS_t new_flags)
{
        if (!handler || !vs)
                return -E_IN_PARAM;
        return vmm_radix_tree_change_leaf_ppn(
                handler, vs, page_va, old_ppn, new_ppn, new_flags);
}
