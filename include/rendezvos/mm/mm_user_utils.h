#ifndef _RENDEZVOS_MM_USER_UTILS_H_
#define _RENDEZVOS_MM_USER_UTILS_H_

#include <common/mm.h>
#include <common/taggedptr.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/error.h>

struct VSpace;

/**
 * @file mm_user_utils.h
 * @brief User VA-range **orchestration templates**: radix + page tables (+ PMM)
 *        in fixed order, with non-trivial rollback where needed.
 *
 * @par Inclusion criterion (do not violate — prevents this module becoming a
 *      grab-bag)
 * A symbol belongs in this file **only if** all of the following hold:
 * -# **Multiple backends** participate in one logical step (at minimum radix
 *    range semantics **and** hardware page-table ops via @c map() / @c unmap()
 *    under @c percpu(Map_Handler); often also PMM and rmap).
 * -# **Failure is not trivial**: partial progress requires an explicit rollback
 *    story (unmap prefix, radix DELETE / restore flags, relink rmap, etc.)
 * **or** a multi-segment walk where a later step failure must undo earlier
 * steps.
 * -# **Callers should not re-encode** map/radix ordering and rollback at every
 *    call site. **L0 (@ref vmm_radix_tree_lock_range_big) is
 * orchestrator-owned** (see @par Radix / L0 vs L2); this file owns L2-only
 * acquire/release via
 *    @ref vmm_radix_tree_lock_range_small_with_big_locked.
 *
 * If an operation is only @c vmm_radix_tree_lock_range_big_and_small (or one
 * radix call under a single L2 acquire) with no PTE/PMM orchestration, it does
 * **not** belong here — call @c vmm_radix_tree_* directly.
 *
 * @par Map handler
 * All paths use the **current CPU's** @c Map_Handler (@c &percpu(Map_Handler)).
 *
 * @par Radix / L0 vs L2 (caller contract)
 * The **orchestrator** (e.g. fork, flag walk, loader) holds @ref
 * vmm_radix_tree_lock_range_big on the VA span it is driving, uses @ref
 * vmm_radix_tree_find_first_occupied_interval (or a known fresh range) to pick
 * one **contiguous, same-flags** sub-range, then calls @c mm_user_utils_* on
 * that half-open interval. Utils take **only** L2 (@ref
 * vmm_radix_tree_lock_range_small_with_big_locked / @ref
 * vmm_radix_tree_unlock_range_small); they never acquire or release L0. When
 * the orchestrator finishes its pass it unlocks L0. L0 serializes who may enter
 * the tree and supports interval discovery; L2 covers radix+map work on one
 * range.
 *
 * @par Zero-fill while CR3 is kernel
 * @ref mm_user_utils_set_range_and_fill zeroes via @ref map_handler_zero_page
 * (not @c memset on user VA); see @ref map_handler_copy_page for the copy side.
 *
 * Every @c mm_user_utils_* entry point assumes L0 is already held on @c [
 * ROUND_DOWN(first_page_va, HUGE_PAGE_SIZE) , vaddr_end ) for the util's page
 * count (@ref vmm_radix_tree_calculate_end_check). The caller may release L0
 * only after the util returns (and may run an unmap loop between L2 critical
 * sections, e.g. @ref mm_user_utils_clean_range_and_unfill).
 *
 * @par Contracts (what these primitives guarantee — not Linux policy)
 * - @ref mm_user_utils_set_range_and_fill: one **contiguous** VA range; only
 *   @p page_count pages are mapped; zero-fill (via map_handler window) runs
 * after L2 unlock. If the
 * radix INSERT lock sees **any** already-insertable overlap in that range, the
 * whole call **fails** (no overwrite, no partial range). PMM alloc before lock
 *   failure is rolled back.
 * - @ref mm_user_utils_fill_page_with_exist_range: exactly **one** page; radix
 * leaf must already exist and be LAZY without VALID. Otherwise **fails** — no
 * implicit grow.
 * - Sparse or multi-hole VA: **not** hidden inside these functions; the
 *   orchestrator composes multiple interval walks / per-page calls.
 * - @ref mm_user_utils_set_range_flags: every page in the half-open range must
 *   already be wired (radix @c VALID + L3 @c have_mapped), **same** radix
 * shadow flags and **same** PTE flags on every page (uniform range, typically
 * from
 *   @ref vmm_radix_tree_find_first_occupied_interval). Holes or huge pages
 *   return error; all-or-nothing update with PTE rollback only (no heap).
 */

/** How @ref mm_user_utils_set_range_flags combines @p set_mask / @p clear_mask.
 */
typedef enum {
        /** @c set_mask is the new stored flags (after @c
           entry_flags_rm_sw_flags). */
        MM_USER_RANGE_FLAGS_ABSOLUTE = 0,
        /** @c desired = (old | set_mask) & ~clear_mask on PTE and on radix. */
        MM_USER_RANGE_FLAGS_DELTA = 1,
        /** Same delta on PTE only; radix shadow unchanged (e.g. fork/COW prep).
         */
        MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY = 2,
} mm_user_range_flags_mode_t;

/**
 * @name Map a contiguous user VA range (PMM + radix + PTE + bind VALID)
 * @{
 */

/**
 * @brief Allocate contiguous physical pages, map+bind @p page_count user pages.
 *
 * **Pre:** caller holds @ref vmm_radix_tree_lock_range_big on the range's L0
 * span; uses L2 @ref RADIX_RL_INSERT → insert_range → map loop →
 * leaf_bind_range → unlock L2 → zero @p page_count bytes at @p range_start.
 * Buddy may satisfy more than @p page_count; excess physical pages are freed
 * immediately after alloc; only @p page_count pages are mapped. On failure:
 * unmap prefix, DELETE range, pmm_free.
 *
 * @return @p range_start on success, or 0 on failure.
 */
vaddr mm_user_utils_set_range_and_fill(struct VSpace* vs, vaddr range_start,
                                       size_t page_count, ENTRY_FLAGS_t flags);

/** @} */

/**
 * @name Map one reserved (LAZY) user page
 * @{
 */

/**
 * @brief Turn one LAZY radix leaf into a zero-filled mapped page (VALID).
 *
 * Leaf must exist, be LAZY, not VALID. Zeros @p page_va after L2 unlock (mapped
 * user VA). **Pre:** same L0 contract as @ref mm_user_utils_set_range_and_fill.
 */
error_t mm_user_utils_fill_page_with_exist_range(struct VSpace* vs,
                                                 vaddr page_va,
                                                 ENTRY_FLAGS_t leaf_flags);

/** @} */

/**
 * @name Unmap / drop a contiguous user VA range
 * @{
 */

/**
 * @brief Unbind radix, unmap PTEs, DELETE range, pmm_free.
 *
 * **Pre:** caller holds @ref vmm_radix_tree_lock_range_big; must
 * @ref vmm_radix_tree_unlock_range_big on all exit paths (including unmap
 * failure — no DELETE, no @c pmm_free).
 */
error_t mm_user_utils_clean_range_and_unfill(struct VSpace* vs,
                                             vaddr range_start,
                                             size_t page_count,
                                             ppn_t ppn_first);

/** @} */

/**
 * @name Single-page remap (PTE + radix + rmap + optional old-page free)
 * @{
 */

/**
 * @brief Replace physical page and/or PTE/radix flags for one mapped user page.
 *
 * **Pre:** caller holds big on the page's L0 span. PPN change uses @c
 * vmm_radix_tree_change_leaf_ppn then @c map(..., @c PAGE_ENTRY_REMAP) (legacy
 * @c nexus_update_node order); not @c unmap+@c map.
 */
error_t mm_user_utils_remap_page(struct VSpace* vs, vaddr page_va,
                                 ppn_t new_ppn, ENTRY_FLAGS_t new_flags,
                                 ppn_t expect_old_ppn);

/** @} */

/**
 * @name Uniform-range entry flags (PTE + radix shadow)
 * @{
 */

/**
 * @brief Update PTE (and radix unless @c MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY) on
 * a contiguous range with **identical** per-page flags beforehand.
 *
 * **Pre:** caller holds big for the whole range (usually after @ref
 * vmm_radix_tree_find_first_occupied_interval).
 */
error_t mm_user_utils_set_range_flags(struct VSpace* vs, vaddr range_start,
                                      u64 length_bytes,
                                      mm_user_range_flags_mode_t mode,
                                      ENTRY_FLAGS_t set_mask,
                                      ENTRY_FLAGS_t clear_mask);

/** @} */

#endif /* _RENDEZVOS_MM_USER_UTILS_H_ */
