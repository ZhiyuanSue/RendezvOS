/*
 * Default user anonymous page backend (see mm_anon_backend.h).
 * Orchestrates radix + map + bind; does not own vspace lifecycle (vmm).
 */
#include <common/align.h>
#include <common/taggedptr.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/mm_anon_backend.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/vmm_radix_tree.h>
#include <rendezvos/smp/percpu.h>

vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags)
{
        if (!vs || vs == &root_vspace || !vs->pmm || page_num == 0
            || ROUND_DOWN(uva, PAGE_SIZE) != uva || !vs->vspace_root_addr
            || !vs->root_radix) {
                return 0;
        }

        struct map_handler* handler = &percpu(Map_Handler);
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
                        "[MM_ANON] mm_user_anon_map_pages: pmm_alloc failed\n");
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
                                "[MM_ANON] mm_user_anon_map_pages: map fail page %lu\n",
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
                        "[MM_ANON] mm_user_anon_map_pages: insert_bind_range failed\n");
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
