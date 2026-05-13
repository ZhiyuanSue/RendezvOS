#ifndef _RENDEZVOS_VMM_RADIX_TREE_H_
#define _RENDEZVOS_VMM_RADIX_TREE_H_

#include <common/types.h>
#include <common/mm.h>
#include <common/dsa/list.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/error.h>
#include <common/taggedptr.h>

/**
 * @file vmm_radix_tree.h
 * @brief Per-vspace radix tree metadata for virtual memory (4 levels, 512-way;
 *        L0..L3 indices are defined in common/mm.h).
 *
 * @par Table layout
 * - L0, L1, L2: each level is one 4KiB page holding 512 @ref Radix_entry_t.
 *   Bit0 = CAS lock, bit1 = VALID, bits 2..11 = child occupancy count,
 *   bits 12..63 = child kernel virtual address (low 12 bits clear).
 * - L3: 512 @ref Radix_node_t per L2 row (32 bytes per node): shadow state per
 *   4KiB user slot (`flags`, `rmap_list`, `owner`).
 *
 * @par Recommended call order
 * -# @ref vmm_radix_tree_init (and optionally @ref
 *    vmm_radix_tree_bootstrap_shared_kernel_high_half /
 *    @ref vmm_radix_tree_install_shared_kernel_high_half for shared high-half
 *    L1). `handler` maps radix table pages at KERNEL linear addresses.
 * -# @ref vmm_radix_tree_calculate_end_check for each VA band, then @ref
 *    vmm_radix_tree_insert_range on @c [ @p vaddr_start , @p vaddr_end ):
 *    grows path as needed (@ref RADIX_RL_INSERT), reserves each crossed leaf as
 *    LAZY, sets `owner` to the caller vspace for L0 index &lt; 256, else to
 *    `root_vspace` for the shared high half (L0 index &gt;= 256). Callers do
 *    not need PTEs yet.
 * -# @ref vmm_radix_tree_leaf_bind_range (or @ref vmm_radix_tree_leaf_bind for
 *    one page): one internal range lock over the same VA span
 *    (@ref RADIX_RL_QUERY_OR_CHANGE). Caller must have mapped PTEs; PPNs must
 *    be contiguous (`ppn_first`, `ppn_first+1`, …). Each leaf must be LAZY and
 *    not VALID; do not require `owner` to match `vs` (shared L3 may differ).
 *    Bind sets VALID + rmap only; it does not rewrite `owner`. Remap/overwrite
 *    uses @c change_* APIs, not bind. Use the same `leaf_flags` vocabulary as
 *    the prior @ref vmm_radix_tree_insert_range on that band for rollback.
 * -# @ref vmm_radix_tree_change_leaf_ppn / @ref vmm_radix_tree_change_range_flag
 *    / @ref vmm_radix_tree_query_leaf as needed (caller holds @ref
 *    vmm_radix_tree_lock_range_small with @ref RADIX_RL_QUERY_OR_CHANGE on the
 *    VA band, then @ref vmm_radix_tree_unlock_range_small).
 * -# @ref vmm_radix_tree_leaf_unbind_range (or @ref vmm_radix_tree_leaf_unbind):
 *    same range lock; INC walk validates each leaf VALID, then flip walker to
 *    DEC (no second init), unlink rmap and clear VALID to LAZY without changing
 *    `owner`.
 * -# To drop radix reservation for a VA band: @ref vmm_radix_tree_lock_range_small
 *    with @ref RADIX_RL_DELETE on that band, then @ref vmm_radix_tree_unlock_range_small
 *    (DELETE work runs inside the internal acquire). Caller removes PTEs first
 *    per policy; then @ref vmm_radix_tree_destroy for full radix teardown (low
 *    half + root only; shared high-half L1 slab is not freed here).
 *
 * @par Internal acquire Phase 4 (L3) by @ref radix_lock_acquire_kind_t
 * - @ref RADIX_RL_INSERT: reject overlap on reserved slots (@c radix_l3_overlap_insert).
 * - @ref RADIX_RL_DELETE: each crossed L3 leaf: @c radix_node_clear if not
 *   @c radix_l3_undeletable (no @c PAGE_ENTRY_VALID — VALID leaves require prior
 *   @ref vmm_radix_tree_leaf_unbind_range or prior rollback to LAZY). On first
 *   undeletable leaf the acquire fails and rolls back per internal cleanup.
 * - @ref RADIX_RL_QUERY_OR_CHANGE: does not bulk-clear L3 nodes; bind/unbind and
 *   flag/query mutate leaves after acquire under the same held L2 set.
 *
 * @par leaf_bind_range / leaf_unbind_range (implementation summary)
 * - leaf_bind_range: single L3 INC walk; per page validate LAZY and not VALID,
 *   then link rmap and set VALID (no `owner` write). On failure, flip walker
 *   to DEC and rollback without re-init (same pattern as internal range-lock
 *   cleanup).
 * - leaf_unbind_range: INC validate to range end, then DEC walk so PPN order
 *   matches VA descending.
 *
 * @par Locking (invariant I6)
 * Multi–2 MiB-band operations take each crossed L2 row bit-lock in ascending
 * 2 MiB base order, then release descending. Do not nest @ref
 * @ref vmm_radix_tree_lock_range_small while holding those locks. Grow path uses nested
 * L0 then L1 then L2 bit-locks (I3); stores to a held row use
 * `radix_entry_update` with lock semantics (I4).
 *
 * @par Shared high-half (I5)
 * L0[256..511] may alias shared L1/L3. A @ref vmm_radix_tree_lock_range_small
 * with @ref RADIX_RL_DELETE clears only leaves whose `owner` matches the
 * caller; @ref vmm_radix_tree_leaf_bind_range
 * and @ref vmm_radix_tree_leaf_unbind_range do not compare `owner` to `vs`.
 * @c change_* APIs follow their documented `owner` rules.
 *
 * @par Child occupancy and reclaim
 * L0/L1/L2 counts count used slots in the next level, not cross-vspace refcounts.
 * Internal Phase 5 adjusts counts when ranges cross 2 MiB bands. Reclaim uses
 * counts as guards when dropping whole L3/L2/L1 pages (I7: structural pages from
 * failed insert are not freed until @ref vmm_radix_tree_destroy, except shared
 * L1 slab).
 *
 * @par Rmap and PMM zone
 * `radix_leaf_link_rmap` / `radix_leaf_unlink_rmap` run under `vs->pmm->zone`
 * lock. Typical order: vspace / radix band work first, then zone (see project
 * INVARIANTS / AI_CHECKLIST); do not invert while the same page is in doubt.
 *
 * @par VA and parameters
 * Half-open intervals use @c [base, end) with @p end as the first byte after the
 * covered pages. Canonical-address policy is the caller's responsibility. APIs
 * that take both `struct map_handler*` and `VSpace*` use handler first, then
 * `vs`.
 * Only @ref vmm_radix_tree_calculate_end_check applies radix VA-band policy to a
 * page count; call it **before** @ref vmm_radix_tree_lock_range_big / @ref
 * vmm_radix_tree_lock_range_small (and matching unlocks) and before mutators that
 * take @c vaddr_end, then pass that same @c vaddr_end for the whole critical
 * section. @ref vmm_radix_tree_lock_range_big, @ref vmm_radix_tree_lock_range_small,
 * @ref vmm_radix_tree_unlock_range_big, @ref vmm_radix_tree_unlock_range_small, and
 * the range mutators do **not** repeat that radix policy check.
 * Mutating range APIs expect the caller to bracket them with @ref
 * vmm_radix_tree_lock_range_small and @ref vmm_radix_tree_unlock_range_small on
 * the same VA interval with the matching @ref radix_lock_acquire_kind_t (see
 * each function's @note).
 *
 * @par Invariants I0–I7 (summary)
 * - I0: Published child KVA in @ref Radix_entry_t is not repointed until
 *   destroy.
 * - I1: Bit0 on each @ref Radix_entry_t (L0/L1/L2) protects that word's value.
 * - I2: One L2 bit-lock covers that row's entry word, L3 array, leaf flags/bind,
 *   and occupancy updates for that 2 MiB band.
 * - I3: Nested grow L0→L1→L2; no lock-order inversion vs other radix locks.
 * - I4: Writes to a held row's value go through `radix_entry_update` (with
 *   INHERIT_LOCK when the CAS lock must survive the store).
 * - I5: Shared high-half: respect `owner` on leaves (see above).
 * - I6: Multi-band: acquire all crossed L2 locks ascending, unlock descending.
 * - I7: Table pages grown on failed insert are not freed until destroy (shared
 *   L1 slab excepted).
 */

#define VMM_RADIX_ENTRY_LOCK_OFF  (0)
#define VMM_RADIX_ENTRY_LOCK_MASK BIT_U64(VMM_RADIX_ENTRY_LOCK_OFF)

#define VMM_RADIX_ENTRY_VALID_OFF  (1)
#define VMM_RADIX_ENTRY_VALID_MASK BIT_U64(VMM_RADIX_ENTRY_VALID_OFF)

#define VMM_RADIX_ENTRY_HUGE_OFF  (11)
#define VMM_RADIX_ENTRY_HUGE_MASK BIT_U64(VMM_RADIX_ENTRY_HUGE_OFF)

#define VMM_RADIX_CNT_SHIFT (2)
#define VMM_RADIX_CNT_MASK  (0x3ffULL << VMM_RADIX_CNT_SHIFT)

#define VMM_RADIX_PTR_MASK (0xfffffffffffff000ULL)

/**
 * @brief One radix table entry word at L0, L1, or L2 (512 entries per 4KiB page).
 */
typedef struct {
        u64 value; /*!< Packed lock, valid, count, and child pointer (see @file). */
} Radix_entry_t;

/**
 * @brief L3 shadow node: one per 4KiB slot under an L2 entry (512 nodes / 16 KiB
 *        of L3 storage per L2 row).
 *
 * @par flags
 * Same @c ENTRY_FLAGS_t vocabulary as map()/PTE: PAGE_ENTRY_READ/WRITE/USER/…
 * describe intended mapping after commit (@ref vmm_radix_tree_insert_range).
 * PAGE_ENTRY_LAZY means metadata reserved without leaf PTE yet;
 * @ref vmm_radix_tree_leaf_bind_range clears LAZY after a successful map().
 * This is not a second page table; it is bookkeeping so fault/query paths can
 * read radix state without walking hardware page tables every time.
 *
 * @par rmap_list
 * Links this leaf into @c Page reverse-map lists when wired to PMM.
 *
 * @par owner
 * Reservation owner from @ref vmm_radix_tree_insert_range (`vs` for L0 index
 * &lt; 256, `root_vspace` for L0 index &gt;= 256 / shared high-half).
 * @ref vmm_radix_tree_leaf_bind_range and @ref vmm_radix_tree_leaf_unbind_range
 * do not change `owner`.
 */
typedef struct {
        ENTRY_FLAGS_t flags;
        struct list_entry rmap_list;
        tagged_ptr_t owner;
} Radix_node_t;

/**
 * @brief Kind argument for @ref vmm_radix_tree_lock_range_small and internal
 *        @c radix_range_lock_acquire (numeric values must stay stable).
 */
typedef enum {
        RADIX_RL_INSERT = 0, /*!< Insert / grow-path semantics at L3. */
        RADIX_RL_DELETE, /*!< Delete-path semantics at L3. */
        /** No L2 occupancy adjustment (mprotect-style metadata / query paths). */
        RADIX_RL_QUERY_OR_CHANGE,
} radix_lock_acquire_kind_t;

/**
 * @brief Compute @p *vaddr_end_out = @p vaddr_start + @p page_number * @c PAGE_SIZE
 *        and run the **only** radix VA-band policy check for that half-open interval.
 *
 * Call this once per VA band before locking, then pass the same @p *vaddr_end_out to
 * @ref vmm_radix_tree_lock_range_big, @ref vmm_radix_tree_lock_range_small,
 * matching unlocks, and to @c vaddr_end on range mutators (insert/bind/unbind,
 * @ref vmm_radix_tree_query_leaf, etc.). Lock/unlock and mutators do not repeat
 * this check.
 *
 * @param vaddr_start   Interval start (inclusive).
 * @param page_number   Number of 4KiB pages (must be >= 1).
 * @param vaddr_end_out Non-NULL; on success, first byte past the band.
 *
 * @return true on success, false if @p page_number is zero, @p vaddr_end_out is NULL,
 *         the span overflows @c vaddr, or @c radix_check_range rejects the band.
 */
bool vmm_radix_tree_calculate_end_check(vaddr vaddr_start, size_t page_number,
                                        vaddr* vaddr_end_out);

/**
 * @brief Acquire coarse L0 bit-locks only for a VA band (512 GiB buckets).
 *
 * @param handler  Map handler; must be non-NULL (same contract as other radix
 *                 entry points that take handler + vs).
 * @param vs       Target vspace whose @c root_radix is initialized.
 * @param vaddr_start  Interval start (page-aligned; interval is [vaddr_start, vaddr_end)).
 * @param vaddr_end    Interval end (first byte not in the interval; same convention
 *                     as internal @c radix_range_lock_acquire). Must match @p *vaddr_end_out
 *                     from a successful @ref vmm_radix_tree_calculate_end_check on the
 *                     same @p vaddr_start and page count (do not hand-roll @c vaddr_start + n *
 *                     @c PAGE_SIZE unless it equals that result).
 *
 * @retval REND_SUCCESS        All crossed L0 entries locked in ascending order.
 * @retval -E_IN_PARAM        Bad pointers or walk setup failed (caller must
 *                            supply @p vaddr_end from @ref vmm_radix_tree_calculate_end_check;
 *                            this function does not re-check radix VA-band policy).
 *
 * @note The vspace radix root already references a full L0 table page; L0 slots
 *       exist. This does not grow L1/L2/L3 and does not imply any L3 leaf path
 *       exists below.
 *
 * @note Must be paired with @ref vmm_radix_tree_unlock_range_big over the same
 *       [vaddr_start, vaddr_end) before leaving the critical section.
 */
error_t vmm_radix_tree_lock_range_big(struct map_handler* handler, VSpace* vs,
                                      vaddr vaddr_start, vaddr vaddr_end);

/**
 * @brief Release L0 bit-locks acquired by @ref vmm_radix_tree_lock_range_big.
 *
 * @param vs    Target vspace.
 * @param vaddr_start Same @p vaddr_start passed to @ref vmm_radix_tree_lock_range_big.
 * @param vaddr_end   Same @p vaddr_end passed to @ref vmm_radix_tree_lock_range_big.
 *
 * @retval REND_SUCCESS     Unlocked all crossed L0 entries (descending walk).
 * @retval -E_IN_PARAM     Bad root or invalid range for the walk.
 */
error_t vmm_radix_tree_unlock_range_big(VSpace* vs, vaddr vaddr_start, vaddr vaddr_end);

/**
 * @brief Find the first 4Ki page in @c [search_start, search_end) whose L3 leaf
 *        satisfies @c radix_l3_overlap_insert (VA order per @p direction).
 *
 * @pre Caller holds @ref vmm_radix_tree_lock_range_big on crossed L0 shards.
 *      Takes @c radix_entry_lock on the chosen L2 row while reading that row and
 *      scanning its L3 slots (see @c clone_vspace radix comment: L0 alone does
 *      not exclude concurrent L2 holders).
 *
 * @param direction @ref RADIX_TREE_DIRECTION_INC or @ref RADIX_TREE_DIRECTION_DEC.
 *
 * @return Non-NULL: first qualifying leaf; @p *out_page_va is that page's VA.
 *         NULL: not found or bad parameters. On success the implementation
 *         releases @c radix_entry_lock on the L2 row before returning; while
 *         the caller still holds @ref vmm_radix_tree_lock_range_big on all L0
 *         shards covering that L2 row (see long comment in @c clone_vspace),
 *         no other core can take that L2 to mutate those L3 nodes, so the
 *         returned pointer and @c Radix_node_t::flags remain usable until big
 *         unlock for that span.
 */
Radix_node_t* vmm_radix_tree_find_first_occupied_leaf(VSpace* vs,
                                                      vaddr search_start,
                                                      vaddr search_end,
                                                      int direction,
                                                      vaddr* out_page_va);

/**
 * @brief Find the first contiguous occupied VA sub-interval inside
 *        @c [search_start, search_end) (half-open, page-aligned).
 *
 * Uses @ref vmm_radix_tree_find_first_occupied_leaf (INC) for the first page,
 * then extends forward in VA under per–2 Mi @c radix_entry_lock / unlock
 * (pattern A: lock one L2 band, scan its L3 leaves, release; next band if the
 * run continues) while each page's leaf still satisfies
 * @c radix_l3_overlap_insert and @c Radix_node_t::flags matches the first
 * page's flags (bitwise @c ENTRY_FLAGS_t equality). **INC-only** extension; DEC
 * is not implemented for runs.
 *
 * @pre Same as @ref vmm_radix_tree_find_first_occupied_leaf (@ref
 *      vmm_radix_tree_lock_range_big held for the search span).
 *
 * @param interval_start_out First page VA of the run (written on success).
 * @param interval_end_out One-past-last page VA of the run (written on
 *                         success); always @c *interval_start_out + N*PAGE_SIZE
 *                         with @c N >= 1.
 *
 * @return @c true on success; @c false if no overlapping leaf exists in range
 *         or parameters are invalid. The maximal run stops where overlap fails
 *         or a leaf's @c flags differ from the first page's (bitwise).
 */
bool vmm_radix_tree_find_first_occupied_interval(VSpace* vs, vaddr search_start,
                                                 vaddr search_end,
                                                 vaddr* interval_start_out,
                                                 vaddr* interval_end_out);

/**
 * @brief Acquire full radix range lock (L2 rows + internal path rules) for a VA
 *        band.
 *
 * @param handler Map handler (radix metadata mapping).
 * @param vs      Target vspace.
 * @param vaddr_start Interval [vaddr_start, vaddr_end) start (page-aligned).
 * @param vaddr_end   Interval end (first byte not covered); same @p vaddr_end as from
 *                   @ref vmm_radix_tree_calculate_end_check for this band.
 * @param kind    Acquire semantics (@ref RADIX_RL_INSERT, @ref RADIX_RL_DELETE,
 *                or @ref RADIX_RL_QUERY_OR_CHANGE).
 *
 * @return Same as internal @c radix_range_lock_acquire (e.g. @c REND_SUCCESS or
 *         an error code).
 *
 * @note On success, only L2 locks remain held until @ref
 *       vmm_radix_tree_unlock_range_small (internal acquire releases L0 after
 *       its phases).
 */
error_t vmm_radix_tree_lock_range_small(struct map_handler* handler, VSpace* vs,
                                        vaddr vaddr_start, vaddr vaddr_end,
                                        radix_lock_acquire_kind_t kind);

/**
 * @brief Release L2 locks held after @ref vmm_radix_tree_lock_range_small.
 *
 * @param vs    Target vspace.
 * @param vaddr_start Same @p vaddr_start as the matching small lock call.
 * @param vaddr_end   Same @p vaddr_end as the matching small lock call.
 *
 * @retval REND_SUCCESS     Released all crossed L2 entries.
 * @retval -E_RENDEZVOS     Walk check failed (should not happen for valid prior
 *                          lock).
 * @retval -E_IN_PARAM      Bad root or invalid range.
 */
error_t vmm_radix_tree_unlock_range_small(VSpace* vs, vaddr vaddr_start, vaddr vaddr_end);

/**
 * @brief Grow radix metadata and lazy-reserve leaf nodes for a contiguous VA
 *        range.
 *
 * @param handler    Map handler; required to map each PMM-backed radix page at
 *                   KERNEL_PHY_TO_VIRT(ppn) (same pattern as nexus).
 * @param vs         Target vspace.
 * @param owner_info Owner tag written into each reserved leaf (`owner` field).
 * @param vaddr_start Start of range (page-aligned).
 * @param flags       Initial shadow flags for each leaf (typically include LAZY).
 * @param vaddr_end   Interval end for @c [ @p vaddr_start , @p vaddr_end ); from
 *                   @ref vmm_radix_tree_calculate_end_check(@p vaddr_start, n_pages,
 *                   &vaddr_end).
 *
 * @return @c REND_SUCCESS on success, or an error code on failure (partial state
 *         is rolled back per implementation).
 *
 * @note Caller must hold @ref vmm_radix_tree_lock_range_small on
 *       [ @p vaddr_start , @p vaddr_end ) with @ref RADIX_RL_INSERT, then @ref
 *       vmm_radix_tree_unlock_range_small after this call returns.
 *
 * @note Per leaf, `owner` is the caller vspace for low-half VA (L0 index &lt;
 *       256) and `root_vspace` for shared kernel high half (L0 index &gt;= 256).
 */
error_t vmm_radix_tree_insert_range(struct map_handler* handler, VSpace* vs,
                                    tagged_ptr_t owner_info, vaddr vaddr_start,
                                    ENTRY_FLAGS_t flags, vaddr vaddr_end);

/**
 * @brief Under one range lock, set VALID + rmap for each lazy-reserved leaf in
 *        a contiguous VA range.
 *
 * @param handler      Map handler.
 * @param vs           Target vspace.
 * @param vaddr_start  Start of range (page-aligned).
 * @param ppn_first    Physical page number of the first page; PPNs must be
 *                     contiguous for the whole range.
 * @param vaddr_end    Interval end for @c [ @p vaddr_start , @p vaddr_end ); from
 *                     @ref vmm_radix_tree_calculate_end_check.
 * @param leaf_flags   Flags applied to every leaf (same vocabulary as @ref
 *                     vmm_radix_tree_insert_range on that band).
 *
 * @return @c REND_SUCCESS or an error code; partial failure rolls back bound
 *         leaves per implementation.
 *
 * @note Typical callers hold @ref vmm_radix_tree_lock_range_small with @ref
 *       RADIX_RL_QUERY_OR_CHANGE on the band, then @ref vmm_radix_tree_unlock_range_small.
 *       The same L2 exclusion is satisfied if the band is already covered by an
 *       earlier @ref RADIX_RL_INSERT lock in the same critical section (e.g. kernel
 *       heap or @c mm_anon_map_pages_eager: @ref vmm_radix_tree_insert_range then
 *       @c map() then this function); this routine does not re-acquire range locks.
 *
 * @note Caller must have wired PTEs for [ @p vaddr_start , @p vaddr_end ). Every
 *       leaf must be LAZY and not VALID beforehand. No map()/unmap() here.
 */
error_t vmm_radix_tree_leaf_bind_range(struct map_handler* handler, VSpace* vs,
                                       vaddr vaddr_start, ppn_t ppn_first,
                                       vaddr vaddr_end, ENTRY_FLAGS_t leaf_flags);

/**
 * @brief Clear VALID and unlink rmap for each page in range; restore LAZY shadow;
 *        does not change `owner`.
 *
 * @param handler      Map handler.
 * @param vs           Target vspace.
 * @param vaddr_start  Start of range.
 * @param ppn_first    PPN of first page (contiguous physical run).
 * @param vaddr_end    Interval end for @c [ @p vaddr_start , @p vaddr_end ); from
 *                     @ref vmm_radix_tree_calculate_end_check.
 *
 * @return @c REND_SUCCESS or an error code.
 *
 * @note Caller must hold @ref vmm_radix_tree_lock_range_small with
 *       @ref RADIX_RL_QUERY_OR_CHANGE on the band, then @ref vmm_radix_tree_unlock_range_small.
 *
 * @note Does not adjust L2 occupancy counts; drop reservation with
 *       @ref vmm_radix_tree_lock_range_small (@ref RADIX_RL_DELETE) and
 *       @ref vmm_radix_tree_unlock_range_small after caller policy (e.g. after unmap).
 */
error_t vmm_radix_tree_leaf_unbind_range(struct map_handler* handler,
                                         VSpace* vs, vaddr vaddr_start,
                                         ppn_t ppn_first, vaddr vaddr_end);

/**
 * @brief Bind a single 4KiB leaf (implemented as @ref vmm_radix_tree_leaf_bind_range
 *        on @c [ @p vaddr_start , @p vaddr_start + PAGE_SIZE )).
 */
error_t vmm_radix_tree_leaf_bind(struct map_handler* handler, VSpace* vs,
                                 vaddr vaddr_start, ppn_t ppn,
                                 ENTRY_FLAGS_t leaf_flags);

/**
 * @brief Unbind a single 4KiB leaf (implemented as @ref
 *        vmm_radix_tree_leaf_unbind_range on @c [ @p vaddr_start , @p vaddr_start + PAGE_SIZE )).
 */
error_t vmm_radix_tree_leaf_unbind(struct map_handler* handler, VSpace* vs,
                                   vaddr vaddr_start, ppn_t ppn);

/**
 * @brief After caller remaps PTE at @p vaddr_start from @p old_ppn to @p new_ppn,
 *        update radix leaf wiring (rmap unlink/link under @c vs->pmm->zone
 *        when PPN changes).
 *
 * @param handler      Map handler.
 * @param vs           Target vspace.
 * @param vaddr_start  User VA of the leaf.
 * @param vaddr_end    One-page band end from @ref vmm_radix_tree_calculate_end_check(
 *                     @p vaddr_start, 1, &vaddr_end).
 * @param old_ppn      Previous PPN.
 * @param new_ppn      New PPN.
 * @param leaf_flags   Shadow flags to record for the leaf.
 *
 * @return @c REND_SUCCESS or an error code.
 *
 * @note Caller must hold @ref vmm_radix_tree_lock_range_small with
 *       @ref RADIX_RL_QUERY_OR_CHANGE on [ @p vaddr_start , @p vaddr_end ), then
 *       @ref vmm_radix_tree_unlock_range_small. No map()/unmap() here.
 */
error_t vmm_radix_tree_change_leaf_ppn(struct map_handler* handler, VSpace* vs,
                                       vaddr vaddr_start, vaddr vaddr_end,
                                       ppn_t old_ppn, ppn_t new_ppn,
                                       ENTRY_FLAGS_t leaf_flags);

/**
 * @brief Like @ref vmm_radix_tree_change_leaf_ppn but also applies @p new_flag
 *        to the leaf flag word (combined remap + flag tweak).
 */
error_t vmm_radix_tree_change_leaf_ppn_flag(struct map_handler* handler,
                                            VSpace* vs, vaddr vaddr_start,
                                            vaddr vaddr_end, ppn_t old_ppn,
                                            ppn_t new_ppn, ENTRY_FLAGS_t new_flag);

/**
 * @brief Metadata-only flag update on a contiguous leaf range (does not grow
 *        tables; skips missing path).
 *
 * @param handler      Map handler (required for @ref vmm_radix_tree_lock_range_small).
 * @param vs           Target vspace.
 * @param vaddr_start  Start of range.
 * @param vaddr_end    Interval end for @c [ @p vaddr_start , @p vaddr_end ); from
 *                     @ref vmm_radix_tree_calculate_end_check.
 * @param new_flags    New flags applied to each existing leaf in range.
 *
 * @return @c REND_SUCCESS or an error code.
 *
 * @note Caller must hold @ref vmm_radix_tree_lock_range_small with
 *       @ref RADIX_RL_QUERY_OR_CHANGE on the band, then @ref vmm_radix_tree_unlock_range_small.
 */
error_t vmm_radix_tree_change_range_flag(struct map_handler* handler, VSpace* vs,
                                         vaddr vaddr_start, vaddr vaddr_end,
                                         ENTRY_FLAGS_t new_flags);

/**
 * @brief Read shadow metadata for one 4KiB slot (does not allocate).
 *
 * @param handler      Map handler (required for @ref vmm_radix_tree_lock_range_small).
 * @param vs           Target vspace.
 * @param vaddr_start  User VA of the slot.
 * @param vaddr_end    One-page band end from @ref vmm_radix_tree_calculate_end_check(
 *                     @p vaddr_start, 1, &vaddr_end).
 * @param out_flags    Optional output for leaf flags (may be NULL).
 * @param out_owner    Optional output for `owner` (may be NULL).
 *
 * @return @c REND_SUCCESS when the L3 leaf was read. Returns @c -E_IN_PARAM
 *         on failure; on failure, non-NULL @p out_flags is cleared to 0 and
 *         non-NULL @p out_owner to tp_new_none() where the implementation applies
 *         that rule.
 *
 * @note Caller must hold @ref vmm_radix_tree_lock_range_small with
 *       @ref RADIX_RL_QUERY_OR_CHANGE on [ @p vaddr_start , @p vaddr_end ), then
 *       @ref vmm_radix_tree_unlock_range_small.
 *
 * @note At least one of @p out_flags and @p out_owner must be non-NULL.
 */
error_t vmm_radix_tree_query_leaf(struct map_handler* handler, VSpace* vs,
                                  vaddr vaddr_start, vaddr vaddr_end,
                                  ENTRY_FLAGS_t* out_flags,
                                  tagged_ptr_t* out_owner);

/**
 * @brief Allocate the L0 radix table page and store its pointer in the vspace
 *        root radix field.
 *
 * @param handler  Map handler: map() wires the page at KERNEL_PHY_TO_VIRT then
 *                 zeroes it.
 * @param vs       Target vspace.
 *
 * @return Pointer to the L0 @ref Radix_entry_t array (root), or NULL on failure.
 */
Radix_entry_t* vmm_radix_tree_init(struct map_handler* handler, VSpace* vs);

/**
 * @brief Tear down radix metadata for @p vs (unmap KERNEL_PHY_TO_VIRT metadata,
 *        free radix pages per policy).
 *
 * @param handler  Map handler (unmap before pmm_free).
 * @param vs       Target vspace.
 *
 * @return @c REND_SUCCESS or an error code.
 *
 * @note Before freeing each low-half L3 table, detaches @ref Radix_node_t::rmap_list
 *       for leaves whose `owner` matches @p vs under the PMM zone lock.
 *       L0[256..511] shared slab is not freed here.
 */
error_t vmm_radix_tree_destroy(struct map_handler* handler, VSpace* vs);

/**
 * @brief One-time bootstrap of shared kernel high-half L1 backing (e.g. BSP
 *        early boot).
 *
 * @param handler  Map handler.
 * @param vs       Non-NULL vspace used for allocation context (see implementation).
 *
 * @return @c REND_SUCCESS or an error code.
 *
 * @note Allocates contiguous pages and map()s each at KERNEL_PHY_TO_VIRT.
 *       Idempotent.
 */
error_t
vmm_radix_tree_bootstrap_shared_kernel_high_half(struct map_handler* handler,
                                                 VSpace* vs);

/**
 * @brief For one vspace after @ref vmm_radix_tree_init, publish L0[256..511] to
 *        the per-index L1 table KVAs from the bootstrapped slab.
 *
 * @param handler  Map handler.
 * @param vs       Target vspace.
 *
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if a slot already points elsewhere,
 *         or another error code.
 *
 * @note Uses per-slot `radix_entry_lock` / `radix_entry_update`; never memcpy
 *       from another vspace's entry words. Requires bootstrap completed first.
 *       Idempotent if already correct. @ref vmm_radix_tree_destroy does not free
 *       this shared band; do not destroy one vspace while others still depend on
 *       the same shared L1 subtree unless policy guarantees disjoint use.
 */
error_t
vmm_radix_tree_install_shared_kernel_high_half(struct map_handler* handler,
                                               VSpace* vs);

#endif
