#ifndef _RENDEZVOS_MM_ANON_BACKEND_H_
#define _RENDEZVOS_MM_ANON_BACKEND_H_

#include <common/mm.h>
#include <common/taggedptr.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/error.h>

struct map_handler;
struct VSpace;

/*
 * Anonymous user-memory backend (C orchestration layer).
 *
 * Role
 * -----
 * Central place for **ordered** sequences: PMM ↔ map() ↔ vmm_radix_tree_* ,
 * matching doc/ai/MM_BACKEND_FRONTEND_API.md intent (L5 “编排”的匿名子集),
 * without yet introducing MMRegion / MMBackend opaque types.
 *
 * Lock / ordering
 * ---------------
 * Callers must respect project lock order (radix range work vs pmm->zone,
 * see vmm_radix_tree.h and INVARIANTS). These helpers do **not** take a
 * vspace-wide mutex by themselves.
 *
 * Naming
 * -------
 * New entry points use prefix `mm_anon_*`. `mm_user_anon_map_pages` remains as
 * the legacy name for existing call sites until migrated.
 */

/* ------------------------------------------------------------------ */
/* Eager: physical pages + PTE + radix VALID in one go                */
/* ------------------------------------------------------------------ */

/*
 * Allocate `page_num` contiguous physical pages, map each PTE in [uva, end),
 * then vmm_radix_tree_insert_bind_range (LAZY shadow + bind + VALID).
 * On any failure: unmap mapped prefix, delete_range radix reservation,
 * pmm_free.
 *
 * Where used (planned / current)
 * --------------------------------
 * - core / loader user stack: thread_loader (today via mm_user_anon_map_pages).
 * - linux_layer: MAP_POPULATE or other “fully materialize now” paths once
 * wired.
 */
vaddr mm_anon_map_pages_eager(struct map_handler* handler, struct VSpace* vs,
                              vaddr uva, size_t page_num, ENTRY_FLAGS_t flags);

/* Legacy name; new code should call mm_anon_map_pages_eager. */
vaddr mm_user_anon_map_pages(struct VSpace* vs, vaddr uva, size_t page_num,
                             ENTRY_FLAGS_t flags);

/* ------------------------------------------------------------------ */
/* Lazy reservation only (radix LAZY + owner; no PTE, no PMM alloc) */
/* ------------------------------------------------------------------ */

/*
 * vmm_radix_tree_insert_range over [page_vaddr, page_vaddr+page_num*PAGE_SIZE).
 * `lazy_flags` vocabulary must match a later eager/fault bind on the same band.
 *
 * Where used
 * ----------
 * - linux_layer sys_mmap anon without MAP_POPULATE (after brk/mmap switch off
 *   get_free_page-only paths).
 * - brk / heap growth when policy is “reserve metadata first, fault later”.
 */
error_t mm_anon_reserve_lazy_range(struct map_handler* handler,
                                   struct VSpace* vs, tagged_ptr_t owner,
                                   vaddr page_vaddr, size_t page_number,
                                   ENTRY_FLAGS_t lazy_flags);

/* ------------------------------------------------------------------ */
/* Demand fill: one 4KiB anonymous zero page on fault                 */
/* ------------------------------------------------------------------ */

/*
 * For a single PAGE_SIZE-aligned faulting VA: ensure radix reservation exists
 * (insert_range if needed), pmm_alloc one page, map(), then leaf_bind_range
 * (or insert_bind_range(1)) so radix VALID + rmap match PTE.
 *
 * Where used
 * ----------
 * - linux_layer/mm/linux_page_fault_irq.c: lazy anonymous zero-fill branch
 *   (replacing hand-rolled pmm_alloc + memset + map + TODO rmap).
 */
error_t mm_anon_zero_fill_fault_page(struct map_handler* handler,
                                     struct VSpace* vs, vaddr fault_page_va,
                                     ENTRY_FLAGS_t leaf_flags);

/* ------------------------------------------------------------------ */
/* Teardown: PTE + radix + PMM for a contiguous anon mapping           */
/* ------------------------------------------------------------------ */

/*
 * Inverse of mm_anon_map_pages_eager for a contiguous band: unmap each PTE,
 * leaf_unbind_range, delete_range, pmm_free(ppn_first, page_number).
 * `ppn_first` must match the mapping radix still records for that VA band.
 *
 * Where used
 * ----------
 * - linux_layer sys_munmap partial/full anonymous segments.
 * - Failure rollback paths (duplicate of today’s fail_unmap_radix_pmm block
 *   in mm_anon_backend.c).
 */
error_t mm_anon_unmap_release_range(struct map_handler* handler,
                                    struct VSpace* vs, vaddr page_vaddr,
                                    size_t page_number, ppn_t ppn_first);

/* ------------------------------------------------------------------ */
/* Query (radix shadow; no map_handler)                               */
/* ------------------------------------------------------------------ */

/*
 * Thin wrapper over vmm_radix_tree_query_leaf: L3 flags + owner for one page.
 * Centralizes call sites so a later “+ PTE snapshot” merge stays one edit.
 *
 * Where used
 * ----------
 * - linux_layer fault / mprotect: replace ad-hoc nexus_query_vaddr once radix
 *   is the sole semantic source for user anon.
 */
error_t mm_anon_query_radix_leaf(struct VSpace* vs, vaddr page_va,
                                 ENTRY_FLAGS_t* out_flags,
                                 tagged_ptr_t* out_owner);

/* ------------------------------------------------------------------ */
/* COW / remap (post–lazy-radix wiring)                                */
/* ------------------------------------------------------------------ */

/*
 * Radix metadata only: vmm_radix_tree_change_leaf_ppn (unlink old Page rmap,
 * link new). Caller must already have installed the new PTE / TLB policy
 * (same split as historical nexus_remap_user_leaf vs PTE).
 *
 * Where used
 * ----------
 * - linux_layer/mm/linux_page_fault_irq.c: linux_handle_cow_fault once
 *   nexus_remap_user_leaf is retired from the anon path.
 */
error_t mm_anon_cow_replace_leaf(struct map_handler* handler, struct VSpace* vs,
                                 vaddr page_va, ppn_t old_ppn, ppn_t new_ppn,
                                 ENTRY_FLAGS_t new_flags);

#endif /* _RENDEZVOS_MM_ANON_BACKEND_H_ */
