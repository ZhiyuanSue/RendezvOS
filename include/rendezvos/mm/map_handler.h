#ifndef _RENDEZVOS_MAP_HANDLER_
#define _RENDEZVOS_MAP_HANDLER_
#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/sync/spin_lock.h>

#define map_pages 0xFFFFFFFFFFE00000
/*
        we use last 2M page as the set of the map used pages virtual addr
        we use one 4K page during the mapping stage
        but consider the multi-core, we use last 2M as this per cpu map page set
        and we think we should not have more than 512 cores
*/
struct map_handler {
        cpu_id_t cpu_id;
        vaddr map_vaddr[4];
        ppn_t ppn_cache[4];
        ppn_t mapped_ppn[4];
        struct pmm* pmm;
        spin_lock_t vspace_lock_node;
};
extern struct map_handler Map_Handler;

/**
 * @brief BSP-only: prepare the shared @c map_pages window and high-half L1
 *        pages needed before per-CPU @c init_map.
 */
error_t sys_init_map(struct pmm* pmm);

/**
 * @brief Initialize one CPU's Map_Handler slots over the shared map_pages
 * window.
 * @param handler Per-CPU handler (usually &per_cpu(Map_Handler, cpu_id)).
 * @param cpu_id  Slot base = cpu_id * 4 within the 2 MiB window.
 * @param pmm     Zone pmm used to refill @c handler->ppn.
 */
error_t init_map(struct map_handler* handler, cpu_id_t cpu_id, struct pmm* pmm);
/**
 * @brief Map or update @p vpn → @p ppn in @p vs page tables.
 *
 * Walks L0→L1→L2→L3 (creating missing table pages from @p handler->ppn_cache
 * as needed) and writes the final leaf for @p level (@c 2 = 2MiB, @c 3 = 4KiB).
 *
 * @par Cases (final leaf already present and VALID)
 * -# **Same @p ppn**: rewrite PTE flags only. Used by mprotect /
 *    @ref mm_user_utils_set_range_flags. @c PAGE_ENTRY_REMAP is **not**
 *    required.
 * -# **Different @p ppn, @c PAGE_ENTRY_REMAP set in @p eflags**: replace the
 *    physical page and flags (COW / @ref mm_user_utils_remap_page after radix
 *    has accepted the new PPN).
 * -# **Different @p ppn, no @c PAGE_ENTRY_REMAP**: fail (refuse silent steal
 *    of an existing mapping).
 *
 * @par Cases (no valid final leaf yet)
 * -# Establish a new @p vpn → @p ppn mapping with @p eflags (minus software
 *    bits). Intermediate levels are allocated from the handler cache as needed.
 *
 * Software-only bits in @p eflags (@c PAGE_ENTRY_REMAP, @c PAGE_ENTRY_LAZY,
 * @c PAGE_ENTRY_COW, …) are stripped before the hardware PTE is written.
 *
 * On failure, intermediate page-table pages already installed are not
 * rolled back, we think kernel PT growth is bounded and user PT is freed when
 * address space is teardown.
 *
 * @param vs      Target address space (must already have a root).
 * @param ppn     Physical page number to map (or keep, for flags-only update).
 * @param vpn     Virtual page number.
 * @param level   @c 2 for 2MiB leaf, @c 3 for 4KiB leaf.
 * @param eflags  Desired entry flags (+ optional @c PAGE_ENTRY_REMAP).
 * @param handler Per-CPU map handler (typically @c &percpu(Map_Handler)).
 *
 * @return @c REND_SUCCESS on success; negative @c error_t on failure.
 */
error_t map(VSpace* vs, ppn_t ppn, vpn_t vpn, int level, ENTRY_FLAGS_t eflags,
            struct map_handler* handler);
/**
 * @brief Clear the leaf mapping for @p vpn in @p vs (and shootdown TLB).
 *
 * @param vs             Target address space.
 * @param vpn            Virtual page number to clear.
 * @param new_entry_addr After zeroing the leaf, written into the PTE @c paddr
 *                       field only; the entry stays invalid (not a live remap).
 * @param handler        Per-CPU map handler.
 * @return Former mapped ppn on success (may be 0 if the L3 leaf was empty);
 *         negative @c error_t on walk failure. Null @p vs/@p handler → 0.
 */
ppn_t unmap(VSpace* vs, vpn_t vpn, u64 new_entry_addr,
            struct map_handler* handler);

/**
 * @brief Query whether @p vpn is mapped in @p vs.
 *
 * @param vs               Address space.
 * @param vpn              Virtual page number.
 * @param entry_flags_out  Optional: portable flags of the leaf (may be NULL).
 * @param entry_level_out  Optional: 2 (huge) or 3 (4K) (may be NULL).
 * @param handler          Per-CPU map handler.
 * @return Mapped ppn (>0); 0 if not present / invalid; negative on error.
 */
ppn_t have_mapped(VSpace* vs, vpn_t vpn, ENTRY_FLAGS_t* entry_flags_out,
                  int* entry_level_out, struct map_handler* handler);

/**
 * @brief Temporarily map a physical page into a per-CPU handler mapping window.
 *
 * @param handler Per-CPU map handler.
 * @param slot_id Window slot index in [0, 4). Caller must avoid conflicts with
 *                concurrent @c map / @c unmap / @c have_mapped on this handler.
 * @param ppn     Physical page number to map into the slot.
 *
 * @return Kernel virtual address for the mapped page, or 0 on error.
 */
vaddr map_handler_map_slot(struct map_handler* handler, int slot_id, ppn_t ppn);

/**
 * @brief Invalidate the mapping window slot after use.
 *
 * @param handler Per-CPU map handler.
 * @param slot_id Window slot index in [0, 4).
 */
void map_handler_unmap_slot(struct map_handler* handler, int slot_id);

/**
 * @brief Zero one physical page via mapping-window slot 0.
 *
 * Same idea as @ref map_handler_copy_page / @ref map_handler_copy_data_range:
 * only @p ppn matters; active CR3 / which @c VSpace is loaded is irrelevant.
 */
error_t map_handler_zero_page(struct map_handler* handler, ppn_t ppn);

/**
 * @brief Copy a physical memory range using the per-CPU mapping window.
 *
 * This is a low-level helper for COW split / vspace clone / page migration.
 * It does not assume a permanent phys->kva mapping.
 *
 * @param handler   Per-CPU map handler.
 * @param dst_paddr Destination physical address (may be unaligned).
 * @param src_paddr Source physical address (may be unaligned).
 * @param len       Bytes to copy (@c 0 → @c -E_IN_PARAM).
 *
 * @return @c REND_SUCCESS on success; negative error code on failure.
 */
error_t map_handler_copy_data_range(struct map_handler* handler,
                                    paddr dst_paddr, paddr src_paddr, u64 len);

static inline error_t map_handler_copy_page(struct map_handler* handler,
                                            ppn_t dst_ppn, ppn_t src_ppn)
{
        return map_handler_copy_data_range(
                handler, PADDR(dst_ppn), PADDR(src_ppn), PAGE_SIZE);
}

/**
 * @brief Copy between a user VA range in @p vs and a kernel buffer.
 *
 * Resolves each user page via @c have_mapped (4 KiB leaves only), then copies
 * through the mapping window. Independent of which address space is loaded.
 *
 * @param vs      User address space (not @c root_vspace ).
 * @param user_va User virtual address.
 * @param kbuf    Kernel buffer.
 * @param len     Bytes to copy ( @c 0 succeeds immediately).
 * @param to_user @c true: @p kbuf → user; @c false: user → @p kbuf.
 *
 * @return @c REND_SUCCESS, or negative @c error_t (unmapped / non-4K leaf).
 * No roll back if in some page copy have an error, the data have mapped will
 * not delete
 */
error_t map_handler_user_kernel_copy(VSpace* vs, u64 user_va, void* kbuf,
                                     size_t len, bool to_user);

/**
 * @brief Allocate a new top-level (L0) page-table root for a table vspace.
 *
 * Layering: this is a **low-level** primitive. It always seeds the **kernel
 * half** of the L0 page from the shared template (`L0_table`). The **user
 * half** (lower half of that same physical page in this layout) is either
 * zeroed or **shallow-copied** from another root — see below.
 *
 * @param old_vs_root_paddr  Physical address of an **existing** L0 root to
 *                           copy from, or 0.
 *   - **0**: `memset` the user half of the new L0 to empty (typical new user
 *     address space before radix tree/map fills it).
 *   - **Non-0**: `memcpy` the **user half of L0 only** (first-level entries)
 *     from that root into the new root. This duplicates **pointers** into the
 *     same lower-level page tables as the source; it is **not** a full
 *     vspace clone and does **not** by itself establish COW or radix tree
 *     truth. Upper layers (e.g. `clone_vspace` + radix tree) own fork/COW
 *     semantics.
 *
 * @param handler  Per-CPU map handler (must match the CPU doing the alloc).
 *
 * @return Physical address of the new L0 root, or 0 on failure.
 *
 * AArch64 note: TTBR0/TTBR1 split means “kernel half” here is layout/policy;
 * we still fill it for a uniform API across architectures.
 */
paddr new_vs_root(paddr old_vs_root_paddr, struct map_handler* handler);

/**
 * @brief Free empty user-half page-table pages under @p vs.
 *
 * Walks user L0→L1→L2→L3; frees a table page only when all children are empty.
 * Leaves VALID leaf PTEs untouched and returns @c -E_RENDEZVOS if any remain
 * (caller must have unmapped leaves first, e.g. via radix clean).
 * Ends with an ASID-wide TLB invalidate for @p vs.
 *
 * @return @c REND_SUCCESS if the user half is empty; @c -E_IN_PARAM /
 *         @c -E_RENDEZVOS otherwise.
 */
error_t vspace_free_user_pt(VSpace* vs, struct map_handler* handler);

/**
 * @brief Free the L0 root physical page and clear @c vs->vspace_root_addr.
 * @note Does not walk or free lower-level tables; call after
 * vspace_free_user_pt teardown.
 */
error_t vspace_free_root_page(VSpace* vs, struct map_handler* handler);

#endif
