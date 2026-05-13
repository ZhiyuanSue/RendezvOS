#ifndef _RENDEZVOS_MM_ANON_BACKEND_H_
#define _RENDEZVOS_MM_ANON_BACKEND_H_

#include <common/mm.h>
#include <common/taggedptr.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/error.h>

struct map_handler;
struct VSpace;

/**
 * @file vmm_anon_backend.h
 * @brief Anonymous user-memory backend (C orchestration layer).
 *
 * @par Role
 * Central place for ordered sequences: PMM, map()/unmap(), and
 * vmm_radix_tree_*(), matching doc/ai/MM_BACKEND_FRONTEND_API.md (L5
 * orchestration for the anonymous subset), without MMRegion / MMBackend opaque
 * types yet.
 *
 * @par Locking and ordering
 * Callers must respect project lock order (radix range work vs pmm->zone;
 * see vmm_radix_tree.h and INVARIANTS). These helpers do not take a vspace-wide
 * mutex by themselves.
 *
 * @par Naming
 * New entry points use prefix `mm_anon_*`. mm_user_anon_map_pages remains as
 * the legacy name for existing call sites until migrated.
 */

/**
 * @name Eager mapping (physical pages + PTE + radix VALID)
 * @{
 */

/**
 * @brief Allocate contiguous physical pages, install PTEs, then radix bind.
 *
 * Allocates @p page_num contiguous physical pages, maps each PTE in
 * @c [uva, end), then calls vmm_radix_tree_insert_bind_range (LAZY shadow +
 * bind + VALID). On failure: unmaps the mapped prefix, takes
 * vmm_radix_tree_lock_range_small(RADIX_RL_DELETE) plus unlock on the VA band,
 * then pmm_free.
 *
 * @param handler   Per-CPU map handler (page-table walk context).
 * @param vs        Target user address space (not root_vspace).
 * @param uva       Page-aligned user virtual start.
 * @param page_num  Number of pages.
 * @param flags     Leaf PTE / software flags; merged with VALID as needed.
 *
 * @return @p uva on success, or 0 on failure.
 *
 * @note Intended call sites: core/loader user stack (thread_loader via
 *       mm_user_anon_map_pages today); linux_layer MAP_POPULATE or similar
 *       eager materialization once wired.
 */
vaddr mm_anon_map_pages_eager(struct map_handler* handler, struct VSpace* vs,
                              vaddr uva, size_t page_num, ENTRY_FLAGS_t flags);

/**
 * @brief Legacy wrapper for mm_anon_map_pages_eager.
 *
 * @param vs         Target user address space.
 * @param uva        Page-aligned user virtual start.
 * @param page_num   Number of pages.
 * @param flags      Leaf / software flags.
 *
 * @return Same as mm_anon_map_pages_eager.
 *
 * @deprecated New code should call mm_anon_map_pages_eager.
 */
vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags);

/** @} */

/**
 * @name Lazy reservation (radix LAZY + owner; no PTE, no PMM alloc)
 * @{
 */

/**
 * @brief Reserve a VA band in the radix only (lazy anonymous).
 *
 * After vmm_radix_tree_calculate_end_check(@p page_vaddr, @p page_number,
 * &vaddr_end), calls vmm_radix_tree_insert_range on
 * @c [page_vaddr, vaddr_end). @p lazy_flags must match a later eager or fault
 * bind on the same band.
 *
 * @param handler      Per-CPU map handler.
 * @param vs           Target address space.
 * @param owner        Reservation owner (tagged pointer).
 * @param page_vaddr   Page-aligned range start.
 * @param page_number  Number of pages.
 * @param lazy_flags   Radix leaf vocabulary for lazy state.
 *
 * @return REND_SUCCESS on success, or an error code (including unlock errors
 *         after a failed insert, per implementation).
 *
 * @note Intended call sites: linux_layer anon mmap without MAP_POPULATE; brk /
 *       heap growth when policy is reserve metadata first, fault later.
 */
error_t mm_anon_reserve_lazy_range(struct map_handler* handler,
                                   struct VSpace* vs, tagged_ptr_t owner,
                                   vaddr page_vaddr, size_t page_number,
                                   ENTRY_FLAGS_t lazy_flags);

/** @} */

/**
 * @name Demand fill (one 4 KiB anonymous zero page on fault)
 * @{
 */

/**
 * @brief Placeholder for lazy anonymous zero-fill on fault.
 *
 * For a single PAGE_SIZE-aligned faulting VA: would ensure radix reservation,
 * pmm_alloc one page, map(), then leaf_bind_range or insert_bind_range so radix
 * VALID and rmap match the PTE. Not wired until lazy radix policy is finalized.
 *
 * @param handler        Per-CPU map handler.
 * @param vs             Target address space.
 * @param fault_page_va  Faulting page-aligned VA.
 * @param leaf_flags     Flags for the materialized leaf.
 *
 * @return -E_RENDEZVOS until implemented.
 *
 * @note Intended: linux_layer/mm/linux_page_fault_irq.c lazy anon zero-fill.
 */
error_t mm_anon_zero_fill_fault_page(struct map_handler* handler,
                                     struct VSpace* vs, vaddr fault_page_va,
                                     ENTRY_FLAGS_t leaf_flags);

/** @} */

/**
 * @name Teardown (PTE + radix + PMM for a contiguous anon mapping)
 * @{
 */

/**
 * @brief Tear down a contiguous anonymous mapping (inverse of eager map).
 *
 * Same radix–PTE order as core/kernel/mm/kmalloc.c core_free_pages:
 * vmm_radix_tree_calculate_end_check, vmm_radix_tree_lock_range_small
 * (RADIX_RL_QUERY_OR_CHANGE), vmm_radix_tree_leaf_unbind_range, unlock, unmap
 * each PTE, RADIX_RL_DELETE lock pair, then pmm_free. @p ppn_first must match
 * what the radix still records for that VA band.
 *
 * @param handler      Per-CPU map handler.
 * @param vs           Target address space.
 * @param page_vaddr   Page-aligned range start.
 * @param page_number  Number of pages.
 * @param ppn_first    Physical page number of the first page in the band.
 *
 * @return REND_SUCCESS if the full sequence including pmm_free succeeds;
 *         otherwise an error from the failing step (see implementation logs).
 *
 * @note Intended: linux_layer sys_munmap for anon; failure rollback paths that
 *       mirror this ordering.
 */
error_t mm_anon_unmap_release_range(struct map_handler* handler,
                                    struct VSpace* vs, vaddr page_vaddr,
                                    size_t page_number, ppn_t ppn_first);

/** @} */

/**
 * @name Query (radix shadow; lock/unlock inside implementation)
 * @{
 */

/**
 * @brief Query radix L3 flags and owner for one page.
 *
 * Thin wrapper over vmm_radix_tree_query_leaf. Centralizes call sites so a
 * later merge with PTE snapshot stays a single edit.
 *
 * @param vs          Target address space.
 * @param page_va     Page-aligned virtual address.
 * @param[out] out_flags  Receives L3 entry flags on success.
 * @param[out] out_owner  Receives owner tagged pointer on success.
 *
 * @return REND_SUCCESS or an error from lock/query path.
 *
 * @note Intended: linux_layer fault / mprotect once radix is the sole semantic
 *       source for user anon.
 */
error_t mm_anon_query_radix_leaf(struct VSpace* vs, vaddr page_va,
                                 ENTRY_FLAGS_t* out_flags,
                                 tagged_ptr_t* out_owner);

/** @} */

/**
 * @name COW / remap (post–lazy-radix wiring)
 * @{
 */

/**
 * @brief Update radix leaf PPN after COW or remap (metadata only).
 *
 * Calls vmm_radix_tree_change_leaf_ppn (unlink old Page rmap, link new). The
 * caller must already have installed the new PTE and TLB policy (same split as
 * historical nexus_remap_user_leaf vs PTE).
 *
 * @param handler    Per-CPU map handler.
 * @param vs         Target address space.
 * @param page_va    Page-aligned virtual address.
 * @param old_ppn    Previous physical page number.
 * @param new_ppn    New physical page number.
 * @param new_flags  New leaf flags for the radix entry.
 *
 * @return REND_SUCCESS or an error from lock/change/unlock path.
 *
 * @note Intended: linux_layer/mm/linux_page_fault_irq.c linux_handle_cow_fault
 *       once nexus_remap_user_leaf is retired from the anon path.
 */
error_t mm_anon_cow_replace_leaf(struct map_handler* handler, struct VSpace* vs,
                                 vaddr page_va, ppn_t old_ppn, ppn_t new_ppn,
                                 ENTRY_FLAGS_t new_flags);

/** @} */

#endif /* _RENDEZVOS_MM_ANON_BACKEND_H_ */
