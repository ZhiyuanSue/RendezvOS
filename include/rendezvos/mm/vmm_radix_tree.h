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

/*
 * Per-vspace radix metadata (4-level, 512 fanout; L0..L3 indices =
 * common/mm.h).
 *
 * Data layout (short)
 * -------------------
 * L0/L1/L2: each a 4KiB page of 512 x Radix_entry_t. Bit0 CAS lock, bit1 VALID,
 * bits2..11 child occupancy count, bits12..63 child KVA (low 12 clear).
 * L3: 512 x Radix_node_t (32B) per L2 row — shadow per 4KiB slot: `flags`
 * (ENTRY_FLAGS_t), `rmap_list` (Page rmap), `vs_ptr` (reservation owner).
 *
 * ---------------------------------------------------------------------------
 * Caller contract — recommended order (end-to-end)
 * ---------------------------------------------------------------------------
 * 1) `vmm_radix_tree_init` (+ optional `bootstrap` /
 * `install_shared_kernel_high_half` for shared high-half L1). `handler` wires
 * radix table pages at KERNEL linear. 2) `insert_range` on [page_vaddr,
 * page_vaddr + n*PAGE_SIZE): grows path if needed (RADIX_RL_INSERT), reserves
 * every crossed leaf as LAZY and sets `vs_ptr` to `vs` for L0 index < 256, else
 * to `root_vspace` for the shared high half (L0 index >= 256). Caller does not
 * need PTEs yet. 3) `leaf_bind_range` (or `leaf_bind` for one page): one
 * `radix_range_lock_acquire` over the same VA span (RADIX_RL_QUERY_OR_CHANGE).
 * Caller must have mapped PTEs; PPNs must be **contiguous** (`ppn_first`,
 * `ppn_first+1`, …). Each leaf must be LAZY and **not** VALID; **do not**
 * require `vs_ptr == vs` (shared L3 may differ). Bind sets VALID + rmap only —
 * **does not** rewrite `vs_ptr` (insert already fixed ownership).
 * Overwrite/remap is `change_*`, not bind. Same `leaf_flags` apply to all
 * pages; rollback restores lazy shadow from `leaf_flags` — match the prior
 * `insert_range` flags on that band. 4) `change_leaf_ppn` / `change_range_flag`
 * / `query_leaf` as needed (still under range lock rules where applicable). 5)
 * `leaf_unbind_range` (or `leaf_unbind`): same range lock; INC walk validates
 *    every leaf is VALID, then the same walker flips to DEC (no second init)
 * and unlinks rmap + clears VALID to LAZY without rewriting `vs_ptr`. 6)
 * `delete_range` when dropping reservation entirely (caller removes PTEs first
 *    per policy); then `destroy` for full radix teardown (low half + root only;
 *    shared high-half L1 slab is immortal).
 *
 * `radix_range_lock_acquire` Phase 4 (L3) by kind
 * -----------------------------------------------
 * - INSERT: reject overlap (lazy/valid/vs on reserved slots).
 * - DELETE: require deletable leaf, then `radix_node_clear` each crossed L3
 * slot.
 * - QUERY_OR_CHANGE: **does not** clear or overwrite L3 nodes — bind/unbind and
 *   flag/query mutate leaves only after acquire, under the same held L2 set.
 *
 * `leaf_bind_range` implementation notes (current .c)
 * ----------------------------------------------------
 * Single L3 walk with RADIX_TREE_DIRECTION_INC: per page validate LAZY +
 * !VALID, then `radix_leaf_link_rmap` + set VALID (no `vs_ptr` write). Any
 * failure (validation, `link_rmap`, or short walk vs `page_number`) jumps to
 * rollback: `while (page_index > 0 && walk)` is a no-op when nothing was bound.
 * Rollback: flip only `walk.direction` to DEC (same pattern as
 * `radix_range_lock_acquire` clean_prev), unlink + restore LAZY — no extra
 * `radix_tree_level_walk_init`.
 *
 * `leaf_unbind_range`: validate with INC to the range end, then `direction =
 * DEC` and `do { … } while (remain && walk)` so PPN order matches VA order
 * backward.
 *
 * Locking & SMP (I6)
 * ------------------
 * Any operation crossing multiple 2Mi bands takes each crossed L2 row’s
 * bit-lock in **ascending** 2MiB base order, then releases **descending**. Do
 * not nest `delete_range` while holding those locks. Single-path grow: L0 then
 * L1 then L2 nested bit-locks (I3); updates to a row’s `value` use
 * `radix_entry_update` with the lock bit semantics (I4).
 *
 * Shared high-half (I5)
 * ---------------------
 * L0[256..511] may alias shared L1/L3. `delete_range` still clears only leaves
 * whose `vs_ptr` matches the caller; `leaf_bind_range` / `leaf_unbind_range` do
 * not compare `vs_ptr` to `vs` (insert already chose ownership). `change_*`
 * follow their per-API `vs_ptr` rules.
 *
 * Child occupancy & reclaim
 * -------------------------
 * Counts in L0/L1/L2 entries are **not** cross-vspace refcounts; they count
 * used slots in the next level. Phase 5 adjusts them with `radix_entry_update`
 * + COUNT delta when ranges cross 2Mi bands. Reclaim uses counts as guard for
 * dropping whole L3/L2/L1 pages (I7: structural pages grown on insert failure
 * are not freed until `destroy`, except shared L1 slab).
 *
 * rmap & PMM zone
 * ---------------
 * `radix_leaf_link_rmap` / `radix_leaf_unlink_rmap` run under `vs->pmm->zone`
 * lock. Typical lock order: vspace / radix band work first, then zone (see
 * project INVARIANTS / AI_CHECKLIST); do not invert while holding the same page
 * in doubt.
 *
 * VA / parameters
 * ---------------
 * Only 4KiB-aligned ranges and finite [base,end) are validated;
 * canonical-address policy is the caller’s. Any API taking both `struct
 * map_handler*` and `VSpace*` uses **handler first, then vs**;
 * `change_range_flag` and `query_leaf` use **vs** first (no handler).
 *
 * Invariants (index I0–I7 unchanged in meaning)
 * ----------------------------------------------
 * I0  Published child KVA in Radix_entry_t is not repointed until `destroy`.
 * I1  Bit0 on each Radix_entry_t (L0/L1/L2) protects that word’s `value`.
 * I2  One L2 bit-lock covers that row’s entry word, L3 array, leaf flags/bind,
 *     and occupancy updates for that 2Mi band.
 * I3  Nested grow: L0→L1→L2 bit-lock order; no lock-order inversion vs other
 * radix. I4  Writes to a held row’s `value` go through `radix_entry_update`
 * (+INHERIT_LOCK when the CAS lock must survive the store). I5  Shared
 * high-half: respect `vs_ptr` on leaves (see above). I6  Multi-band: acquire
 * all crossed L2 locks ascending 2Mi base, unlock descending. I7  Table pages
 * grown on failed insert are not freed until `destroy` (shared L1 slab
 * excepted).
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

typedef struct {
        u64 value;
} Radix_entry_t;

/*
 * L3: one node per 4KiB user page slot under an L2 entry (512 nodes / 16KiB).
 *
 * `flags` — shadow leaf state (same ENTRY_FLAGS_t vocabulary as map()/PTE):
 *   - Bits such as PAGE_ENTRY_{READ,WRITE,USER,...} describe the mapping the
 *     range APIs intend for this VA once committed (see insert_range).
 *   - PAGE_ENTRY_NEXUS_LAZY means "metadata reserved, no leaf PTE yet";
 *     leaf_bind / leaf_bind_range clear LAZY after a successful map().
 *   This is not a second page table; it is nexus-side bookkeeping so fault /
 *   query paths can consult the radix without parsing the HW walk every time.
 *
 * `rmap_list` — links this leaf into Page reverse-map lists when wired to PMM.
 * `vs_ptr` — reservation owner set by `insert_range` (`vs` for L0 index < 256,
 *   `root_vspace` for L0 index >= 256 / shared high-half). `leaf_bind_range` /
 *   `leaf_unbind_range` do not change it.
 */
typedef struct {
        ENTRY_FLAGS_t flags;
        struct list_entry rmap_list;
        tagged_ptr_t owner;
} Radix_node_t;

/*
 * Range insert: grow metadata + reserve leaf nodes (lazy via LAZY flag).
 * Per leaf, `vs_ptr` is `vs` for VA in the low half (L0 index < 256), and
 * `root_vspace` for the shared kernel high half (L0 index >= 256).
 * `handler` is required: each PMM-backed radix page is installed with map()
 * at KERNEL_PHY_TO_VIRT(ppn) (same as nexus); grow uses that linear KVA.
 * `root_vs` is used for shared high-half leaves (L0 index >= 256); typically
 * `&root_vspace` when `vs` is a kernel or user table vspace using that slab.
 */
error_t vmm_radix_tree_insert_range(struct map_handler* handler, VSpace* vs,
                                    tagged_ptr_t owner_info, vaddr page_vaddr,
                                    ENTRY_FLAGS_t flags, size_t page_number);

/*
 * Mark a contiguous VA range as committed (sets VALID + rmap; leaves `vs_ptr`
 * as set by `insert_range`), one 4KiB leaf at a
 * time under a single range lock (fewer L2 acquire/release cycles than per-page
 * calls). Caller must have wired PTEs for each page in
 * [page_vaddr, page_vaddr + page_number*PAGE_SIZE) with **contiguous** PPNs
 * `ppn_first, ppn_first+1, ...`. `page_number >= 1`. Same `leaf_flags` apply to
 * every leaf. Every leaf must already be **lazy-reserved** (LAZY, not VALID);
 * `vs_ptr` is whatever `insert_range` set (need not equal `vs`).
 * Remap/overwrite uses `change_*`. Rollback on partial rmap failure restores
 * lazy shadow from `leaf_flags` — use the same `flags` vocabulary as the prior
 * `insert_range` for this VA band. No map()/unmap() here.
 */
error_t vmm_radix_tree_leaf_bind_range(struct map_handler* handler, VSpace* vs,
                                       vaddr page_vaddr, ppn_t ppn_first,
                                       size_t page_number,
                                       ENTRY_FLAGS_t leaf_flags);

/*
 * Restore radix shadow to LAZY for each page in the range (does not change
 * `vs_ptr`). `ppn_first` is the PPN of the first page; physical pages must be
 * contiguous. `page_number >= 1`. Does not adjust L2 occupancy; delete_range
 * removes reservation entirely.
 */
error_t vmm_radix_tree_leaf_unbind_range(struct map_handler* handler,
                                         VSpace* vs, vaddr page_vaddr,
                                         ppn_t ppn_first, size_t page_number);

/* Single 4KiB page; implemented as leaf_*_range(..., 1). */
error_t vmm_radix_tree_leaf_bind(struct map_handler* handler, VSpace* vs,
                                 vaddr page_vaddr, ppn_t ppn,
                                 ENTRY_FLAGS_t leaf_flags);
error_t vmm_radix_tree_leaf_unbind(struct map_handler* handler, VSpace* vs,
                                   vaddr page_vaddr, ppn_t ppn);

/*
 * Delete radix reservation for [page_vaddr, page_vaddr+page_number*PAGE_SIZE).
 * Caller tears down PTEs for that VA range. `handler` is for radix metadata
 * map/free inside radix_range_lock_acquire (not leaf PTEs).
 */
error_t vmm_radix_tree_delete_range(struct map_handler* handler, VSpace* vs,
                                    vaddr page_vaddr, size_t page_number);

/*
 * Update radix leaf flags after caller remaps PTE at `page_vaddr` from
 * `old_ppn` to `new_ppn`. When `old_ppn != new_ppn`, unlinks `rmap_list` from
 * the old `Page` and links into the new (same as leaf_unbind / leaf_bind under
 * `vs->pmm->zone`). No map()/unmap() here.
 */
error_t vmm_radix_tree_change_leaf_ppn(struct map_handler* handler, VSpace* vs,
                                       vaddr page_vaddr, ppn_t old_ppn,
                                       ppn_t new_ppn, ENTRY_FLAGS_t leaf_flags);

error_t vmm_radix_tree_change_leaf_ppn_flag(struct map_handler* handler,
                                            VSpace* vs, vaddr page_vaddr,
                                            ppn_t old_ppn, ppn_t new_ppn,
                                            ENTRY_FLAGS_t new_flag);

/* Metadata-only: no map_handler; does not grow tables (skips missing path). */
error_t vmm_radix_tree_change_range_flag(VSpace* vs, vaddr page_vaddr,
                                         ENTRY_FLAGS_t new_flags,
                                         size_t page_number);

/*
 * Read shadow metadata for one 4KiB slot; no map_handler; does not allocate.
 * `out_flags` and/or `out_owner` may be NULL; at least one must be non-NULL.
 * On success, fills requested outputs from the L3 leaf. On -E_IN_PARAM (no leaf
 * / bad range), clears `*out_flags` to 0 and `*out_owner` to tp_new_none() when
 * the corresponding pointer is non-NULL.
 */
error_t vmm_radix_tree_query_leaf(VSpace* vs, vaddr page_vaddr,
                                  ENTRY_FLAGS_t* out_flags,
                                  tagged_ptr_t* out_owner);

/*
 * Allocate the L0 table page; stores pointer in vs nexus root node.
 * Requires `handler`: map() wires the page at KERNEL_PHY_TO_VIRT then zeros it.
 */
Radix_entry_t* vmm_radix_tree_init(struct map_handler* handler, VSpace* vs);

/*
 * Requires `handler` to unmap KERNEL_PHY_TO_VIRT metadata before pmm_free.
 * In the same teardown walk, before each low-half L3 table is freed, detaches
 * `Radix_node_t::rmap_list` for leaves with `vs_ptr == vs` under the PMM zone
 * lock. L0[256..511] shared slab is not freed here.
 */
error_t vmm_radix_tree_destroy(struct map_handler* handler, VSpace* vs);

/*
 * Shared kernel high-half L1 backing (boot vs every other vspace)
 * ---------------------------------------------------------------
 * - vmm_radix_tree_bootstrap_shared_kernel_high_half: run once (e.g. BSP early
 *   boot). Allocates contiguous pages and map()s each at KERNEL_PHY_TO_VIRT.
 *   Requires non-NULL `vs` and `handler`. Idempotent.
 * - vmm_radix_tree_install_shared_kernel_high_half: for one vspace after
 *   vmm_radix_tree_init, publishes L0[256..511] to the per-index L1 table KVAs
 *   derived from the bootstrapped slab (`radix_entry_lock` /
 * `radix_entry_update` per slot; never memcpy of another vspace’s entry words).
 * Idempotent if already correct; -E_IN_PARAM if a slot already points
 * elsewhere. Install requires bootstrap to have completed. After install,
 * delete_range / leaf_bind / leaf_unbind / change_* on VAs in that band use
 * Radix_node_t::vs_ptr (set by insert_range) so shared L3 is not corrupted by
 * another vspace (see I5 and Range APIs above). vmm_radix_tree_destroy does not
 * free the shared high-half L1 band (L0[256..511]); it only tears down
 * L0[0..255] subtrees and the root page. Do not destroy radix for one vspace
 * while others still depend on the same shared L1 subtree unless policy
 * guarantees disjoint use (e.g. only root_vspace installs).
 */
error_t
vmm_radix_tree_bootstrap_shared_kernel_high_half(struct map_handler* handler,
                                                 VSpace* vs);
error_t
vmm_radix_tree_install_shared_kernel_high_half(struct map_handler* handler,
                                               VSpace* vs);

#endif
