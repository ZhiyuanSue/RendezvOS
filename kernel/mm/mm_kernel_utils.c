#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/mm_kernel_utils.h>

static bool identity_mapped_ppn_legal(ppn_t ppn, int entry_level,
                                      paddr page_phy)
{
        if (invalid_ppn(ppn))
                return false;
        if (entry_level == 3)
                return ppn == (ppn_t)PPN(page_phy);
        if (entry_level == 2)
                return ppn
                       == (ppn_t)PPN(ROUND_DOWN(page_phy, MIDDLE_PAGE_SIZE));
        return false;
}

error_t ensure_kernel_identity_range(struct VSpace *vs, paddr phy, size_t len,
                                     ENTRY_FLAGS_t eflags,
                                     struct map_handler *handler)
{
        ENTRY_FLAGS_t leaf_eflags;
        paddr end, page_start, page_end, page_iter;
        error_t map_error;

        if (!vs || !handler)
                return -E_IN_PARAM;
        if (len == 0)
                return REND_SUCCESS;
        end = phy + len;
        if (end < phy)
                return -E_IN_PARAM;
        page_start = ROUND_DOWN(phy, PAGE_SIZE);
        page_end = ROUND_UP(end, PAGE_SIZE);

        leaf_eflags = entry_flags_rm_sw_flags(eflags);
        if (!(leaf_eflags & PAGE_ENTRY_VALID))
                return -E_IN_PARAM;

        for (page_iter = page_start; page_iter < page_end;
             page_iter += PAGE_SIZE) {
                int entry_level = 0;
                ppn_t ppn = have_mapped(vs,
                                        VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                        NULL,
                                        &entry_level,
                                        handler);

                if (!invalid_ppn(ppn)) {
                        if (!identity_mapped_ppn_legal(
                                    ppn, entry_level, page_iter)) {
                                pr_error(
                                        "[ KMAP ] ERROR: non-identity at phy=0x%lx\n",
                                        (u64)page_iter);
                                return -E_RENDEZVOS;
                        }
                        continue;
                }
                if (ppn != -E_REND_NOFOUND)
                        return (error_t)ppn;

                map_error = map(vs,
                                PPN(page_iter),
                                VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                3,
                                leaf_eflags,
                                handler);
                if (map_error != REND_SUCCESS) {
                        pr_error("[ KMAP ] ERROR: map fail phy=0x%lx e=%d\n",
                                 (u64)page_iter,
                                 map_error);
                        /* No rollback: caller fails the subsystem / boot. */
                        return map_error;
                }
        }
        return REND_SUCCESS;
}

error_t map_kernel_identity_empty_range(struct VSpace *vs, paddr phy,
                                        size_t len, ENTRY_FLAGS_t eflags,
                                        struct map_handler *handler)
{
        ENTRY_FLAGS_t leaf_eflags;
        paddr end, page_start, page_end, page_iter;
        error_t map_error;

        if (!vs || !handler)
                return -E_IN_PARAM;
        if (len == 0)
                return REND_SUCCESS;
        end = phy + len;
        if (end < phy)
                return -E_IN_PARAM;
        page_start = ROUND_DOWN(phy, PAGE_SIZE);
        page_end = ROUND_UP(end, PAGE_SIZE);

        leaf_eflags = entry_flags_rm_sw_flags(eflags);
        if (!(leaf_eflags & PAGE_ENTRY_VALID))
                return -E_IN_PARAM;

        /*
         * One cursor: ++ preflight to page_end, -- map toward page_start;
         * on map fail, ++ again to unmap what this call already installed
         * (pages in (page_iter, page_end)).
         */
        for (page_iter = page_start; page_iter < page_end;
             page_iter += PAGE_SIZE) {
                int entry_level = 0;
                ppn_t ppn = have_mapped(vs,
                                        VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                        NULL,
                                        &entry_level,
                                        handler);

                if (!invalid_ppn(ppn)) {
                        pr_error(
                                "[ KMAP ] ERROR: not empty phy=0x%lx level=%d\n",
                                (u64)page_iter,
                                entry_level);
                        return -E_RENDEZVOS;
                }
                if (ppn != -E_REND_NOFOUND)
                        return (error_t)ppn;
        }

        while (page_iter > page_start) {
                page_iter -= PAGE_SIZE;
                map_error = map(vs,
                                PPN(page_iter),
                                VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                3,
                                leaf_eflags,
                                handler);
                if (map_error != REND_SUCCESS)
                        goto fail_unmap_rollback;
        }
        return REND_SUCCESS;

fail_unmap_rollback:
        pr_error("[ KMAP ] ERROR: map fail phy=0x%lx e=%d\n",
                 (u64)page_iter,
                 map_error);
        for (page_iter += PAGE_SIZE; page_iter < page_end;
             page_iter += PAGE_SIZE)
                (void)unmap(vs, VPN(KERNEL_PHY_TO_VIRT(page_iter)), 0, handler);
        return map_error;
}

error_t unmap_kernel_identity_range(struct VSpace *vs, paddr phy, size_t len,
                                    struct map_handler *handler)
{
        paddr end, page_start, page_end, page_iter;
        error_t error = REND_SUCCESS;

        if (!vs || !handler)
                return -E_IN_PARAM;
        if (len == 0)
                return REND_SUCCESS;
        end = phy + len;
        if (end < phy)
                return -E_IN_PARAM;
        page_start = ROUND_DOWN(phy, PAGE_SIZE);
        page_end = ROUND_UP(end, PAGE_SIZE);

        for (page_iter = page_start; page_iter < page_end;
             page_iter += PAGE_SIZE) {
                int entry_level = 0;
                ppn_t ppn = have_mapped(vs,
                                        VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                        NULL,
                                        &entry_level,
                                        handler);

                if (ppn == -E_REND_NOFOUND)
                        continue;
                if (invalid_ppn(ppn)) {
                        pr_error("[ KMAP ] ERROR: have_mapped phy=0x%lx e=%d\n",
                                 (u64)page_iter,
                                 (int)ppn);
                        error = (error_t)ppn;
                        continue;
                }

                if (entry_level == 3
                    && identity_mapped_ppn_legal(ppn, entry_level, page_iter)) {
                        (void)unmap(vs,
                                    VPN(KERNEL_PHY_TO_VIRT(page_iter)),
                                    0,
                                    handler);
                        continue;
                }

                pr_error("[ KMAP ] ERROR: cannot unmap phy=0x%lx level=%d\n",
                         (u64)page_iter,
                         entry_level);
                error = -E_RENDEZVOS;
        }
        return error;
}
