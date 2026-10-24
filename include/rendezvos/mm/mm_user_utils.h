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
 * @brief User VA helpers: call radix + page tables (+ PMM) in a fixed order,
 *        and roll back on failure when needed.
 *
 * Sibling of @c mm_kernel_utils (identity PTE-only maps for PA not obtained
 * from any zone allocator; no radix / rmap).
 *
 * Put a symbol here only if all of these are true:
 * - One step touches more than one layer (at least radix and @c map / @c unmap
 *   under @c percpu(Map_Handler); often also PMM and rmap).
 * - Failure needs an explicit undo (unmap prefix, radix DELETE / restore,
 *   rmap), or a walk where a later failure must undo earlier steps.
 * - That multi-layer order should live in one place, not be copied at every
 *   caller.
 *
 * Do **not** add wrappers that only forward to a single layer — e.g. one
 * @c vmm_radix_tree_* call, one @c map / @c unmap, or one Map_Handler helper.
 * Call those APIs directly.
 *
 * How to call (L0 / L2) — two helpers; do not mix them up.
 *
 * @par Why this file keeps L0 (trade-off vs the heap path)
 * Fine-grained work always uses L2 (2 MiB bands). The question is only whether
 * L0 (≈512 GiB) may be dropped once L2 is held.
 *
 * - Heap / @c core_get_free_pages uses
 *   @c vmm_radix_tree_lock_range_big_and_small: take L0, take L2, **drop L0
 *   before return**. VA is known up front (identity of allocated pages), one
 *   contiguous shot, no @c find_first_occupied_interval. Holding L0 during
 *   map/bind would only widen the critical section for no gain.
 *
 * - User paths that use this file usually look like: lock L0 → find a
 *   same-flags sub-interval → util (L2) → maybe find the next → util again →
 *   … → unlock L0. @c find_first_occupied_leaf / @c
 *   find_first_occupied_interval require L0 already held, and the returned
 *   leaf stays usable only while that L0 lease remains (see those APIs). If
 *   each util used @c big_and_small and dropped L0 between “find” and “mutate”
 *   (or between two utils), another CPU could change the radix in between —
 *   classic TOCTOU. Keeping L0 is the lease for “who is driving discover +
 *   multi-step work on this shard”; L2 is still what serializes the actual
 *   mutate. Cost: other CPUs that need L0 on the same shard wait. Benefit:
 *   a stable view across interval scan and several utils.
 *
 * So: this is not “utils refuse fine locks”. Utils take L2. They refuse to
 * own L0 because the caller already must, for discovery / multi-step.
 *
 * @par Call pattern for this file
 * Upper code (fork, loader, …): @c vmm_radix_tree_lock_range_big, keep L0.
 * Each util: @c vmm_radix_tree_lock_range_small_with_big_locked (L2 only; does
 * not take or drop L0). After the util returns, upper code may unmap between
 * L2 sections, then @c vmm_radix_tree_unlock_range_big.
 *
 * Every entry point assumes L0 is already held on
 * [ROUND_DOWN(first_page_va, HUGE_PAGE_SIZE), vaddr_end) for the util's page
 * count (@c vmm_radix_tree_calculate_end_check).
 *
 * Map handler: always the current CPU's @c Map_Handler.
 */
typedef enum {
        MM_USER_RANGE_FLAGS_ABSOLUTE = 0,
        MM_USER_RANGE_FLAGS_DELTA = 1,
        MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY = 2,
} mm_user_range_flags_mode_t;

/**
 * @brief Allocate contiguous physical pages, map+bind @p page_count user pages.
 *
 * One contiguous VA range only; maps exactly @p page_count pages; zero-fill
 * after L2 unlock. Any insertable overlap in the range will lead to the whole
 * call fails (no partial map). Sparse holes: issue multiple calls from the
 * upper code.
 *
 * Pre: caller holds @c vmm_radix_tree_lock_range_big on the range's L0 span.
 * Uses L2 INSERT, then maps and binds. On failure: unmaps any prefix, deletes
 * the radix range, frees physical pages.
 *
 * @return @p range_start on success, or 0 on failure.
 */
vaddr mm_user_utils_set_range_and_fill(struct VSpace* vs, vaddr range_start,
                                       size_t page_count, ENTRY_FLAGS_t flags);

/**
 * @brief Turn one LAZY radix leaf into a zero-filled mapped page (VALID).
 *
 * Exactly one page. Already-VALID leaf means success and no-op. Otherwise the
 * leaf must already exist and be LAZY (not VALID). No implicit grow of new
 * reservations.
 *
 * Pre: same L0 contract as @c mm_user_utils_set_range_and_fill.
 */
error_t mm_user_utils_fill_page_with_exist_range(struct VSpace* vs,
                                                 vaddr page_va,
                                                 ENTRY_FLAGS_t leaf_flags);

/**
 * @brief Unbind radix, then unmap PTEs, then DELETE range, then pmm_free.
 *
 * Pre: caller holds L0 big; must unlock big on all exits (including unmap
 * failure — no DELETE, no pmm_free).
 */
error_t mm_user_utils_clean_range_and_unfill(struct VSpace* vs,
                                             vaddr range_start,
                                             size_t page_count,
                                             ppn_t ppn_first);

/**
 * @brief Replace physical page and/or PTE/radix flags for one mapped user page.
 *
 * Pre: caller holds big on the page's L0 span. PPN change uses
 * @c vmm_radix_tree_change_leaf_ppn then @c map(..., PAGE_ENTRY_REMAP).
 */
error_t mm_user_utils_remap_page(struct VSpace* vs, vaddr page_va,
                                 ppn_t new_ppn, ENTRY_FLAGS_t new_flags,
                                 ppn_t expect_old_ppn);

/**
 * @brief Update flags on a contiguous range where every page already has the
 *        same radix shadow flags and the same PTE flags.
 *
 * Every page must already be wired (radix VALID + L3 @c have_mapped). Holes or
 * huge pages → error. All-or-nothing; on failure only PTE changes are rolled
 * back (no heap). Sparse VA: multiple calls from the upper code.
 *
 * @p mode selects how @p set_mask / @p clear_mask apply:
 * - @c MM_USER_RANGE_FLAGS_ABSOLUTE: @p set_mask is the new stored flags
 *   (after stripping software bits).
 * - @c MM_USER_RANGE_FLAGS_DELTA: desired = (old | set_mask) & ~clear_mask on
 *   PTE and on radix.
 * - @c MM_USER_RANGE_FLAGS_DELTA_PTE_ONLY: same delta on PTE only; radix
 *   shadow unchanged (e.g. fork/COW prep).
 *
 * Pre: caller holds big for the whole range (usually after
 * @c vmm_radix_tree_find_first_occupied_interval).
 */
error_t mm_user_utils_set_range_flags(struct VSpace* vs, vaddr range_start,
                                      u64 length_bytes,
                                      mm_user_range_flags_mode_t mode,
                                      ENTRY_FLAGS_t set_mask,
                                      ENTRY_FLAGS_t clear_mask);

#endif /* _RENDEZVOS_MM_USER_UTILS_H_ */
