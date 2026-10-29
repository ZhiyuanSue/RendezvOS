#ifndef _RENDEZVOS_MM_KERNEL_UTILS_H_
#define _RENDEZVOS_MM_KERNEL_UTILS_H_

#include <common/stdbool.h>
#include <common/mm.h>
#include <common/types.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>

/**
 * @file mm_kernel_utils.h
 * @brief Kernel **identity** VA helpers (@c KERNEL_PHY_TO_VIRT): compose
 *        @c have_mapped / @c map / @c unmap only — no radix, no PMM.
 *
 * Sibling of @c mm_user_utils. For physical spans that were **not** obtained
 * from any MemZone’s allocator (@c pmm_alloc) — firmware tables, MMIO/BAR,
 * and similar. Those frames are outside every zone’s free pool, so they must
 * not go through @c leaf_bind / rmap (no zone @c Page[] ownership). Zone-backed
 * RAM (buddy or any other per-zone @c struct pmm) stays on the
 * @c core_get_free_pages / radix path.
 *
 * Two install contracts — named by **policy**, not by device class
 * (@p eflags carries attributes such as Device):
 *
 * | Function                          | Policy                         |
 * Already mapped   | Holes            |
 * |-----------------------------------|--------------------------------|------------------|------------------|
 * | @c ensure_kernel_identity_range   | Make span reachable (ENSURE)   |
 * identity → skip  | 4KiB + @p eflags | | @c map_kernel_identity_empty_range|
 * Install into a fully empty span| **any** → fail   | 4KiB + @p eflags |
 *
 * Tear-down for empty-install ranges: @c unmap_kernel_identity_range
 * (best-effort). ENSURE / firmware ranges are not unmapped by design.
 *
 * Never installs a 2MiB leaf (would cover neighboring Available frames).
 *
 * @par Concurrency (why there is no extra lock here)
 * These helpers add **no** range lock, port mutex, or “who owns this PA”
 * protocol on top of what @c have_mapped / @c map / @c unmap already take
 * (@c vspace_lock per PTE walk). That is intentional.
 *
 * The intended callers are **exclusive** for a given physical span: e.g. one
 * CPU running ACPI table bring-up, or one driver instance mapping / unmapping
 * its BAR window. Under that rule, two threads never race on the same
 * @p [phy,phy+len) through these APIs, so a second lock would only serialize
 * unrelated work and complicate teardown.
 *
 * If two parties must touch the same span, the **caller** must serialize
 * (probe lock, device lock, boot single-threaded phase, etc.). Do not assume
 * empty-preflight or ENSURE skip is a substitute for mutual exclusion — a
 * concurrent mapper can still lose the race between @c have_mapped and
 * @c map.
 *
 * @par Common parameters (all three APIs)
 * @param vs       Address space (typically @c &root_vspace / current kernel
 *                 @c VSpace).
 * @param phy      Physical start of the span (need not be page-aligned; rounded
 *                 down to @c PAGE_SIZE).
 * @param len      Byte length; @c 0 is a no-op success. End is rounded up so
 *                 @p [phy, phy+len) is fully covered.
 * @param eflags   (map/ensure only) Portable leaf flags for **new** 4KiB
 *                 entries. Software bits stripped via
 *                 @c entry_flags_rm_sw_flags. Must include
 *                 @c PAGE_ENTRY_VALID. Caller chooses Normal vs Device etc.
 * @param handler  Per-CPU map handler for @c have_mapped / @c map / @c unmap
 *                 (typically @c &percpu(Map_Handler)).
 *
 * @par Common return
 * @return @c REND_SUCCESS or a negative @c error_t. Exact failure cases are
 *         per function.
 */

/**
 * @brief ENSURE @p [phy, phy+len) is reachable via identity VA.
 *
 * Typical callers: ACPI / firmware tables, IOAPIC MMIO windows, and similar
 * spans that stay mapped for the life of the kernel. Boot may already cover
 * part of the span with an identity L2 huge leaf — that is **not** an error.
 *
 * Per page-aligned frame in the rounded span:
 * - @c have_mapped miss (@c -E_REND_NOFOUND) → @c map(...,level3, @p eflags);
 * - already identity (L3 PPN matches, or L2 huge base matches the 2MiB
 *   window) → skip (existing flags are **not** checked or updated);
 * - already mapped, non-identity → fail;
 * - other negative @c have_mapped results → return as-is (do not call @c map).
 *
 * @p eflags applies only to newly installed 4KiB leaves.
 *
 * Mid-loop @c map failure: return the error **without** undoing pages already
 * installed by this call (upper layer fails boot / skips the subsystem).
 *
 * @return @c REND_SUCCESS; or negative on bad parameters, non-identity
 *         collision, @c have_mapped walk error, or @c map failure.
 */
error_t ensure_kernel_identity_range(struct VSpace *vs, paddr phy, size_t len,
                                     ENTRY_FLAGS_t eflags,
                                     struct map_handler *handler);

/**
 * @brief Map @p [phy, phy+len) only if the whole span is currently unmapped.
 *
 * Policy is “empty interval → install 4KiB identity”, not “MMIO”. Drivers
 * pass Device / uncached bits in @p eflags when needed.
 *
 * If any covered page is already present (L2, L3, or non-identity), the call
 * fails and installs nothing. After a successful preflight, every page is
 * mapped at level 3 with @p eflags.
 *
 * Mid-loop @c map failure: best-effort unmap of pages this call already
 * installed (span was empty, so undo does not touch strangers), then return
 * the error. A dirty leftover still blocks the next
 * @c map_kernel_identity_empty_range (fuse).
 *
 * @return @c REND_SUCCESS if the whole span was empty and is now mapped;
 *         negative if preflight finds any present page, on bad parameters, or
 *         if @c map fails mid-loop (after best-effort undo).
 */
error_t map_kernel_identity_empty_range(struct VSpace *vs, paddr phy,
                                        size_t len, ENTRY_FLAGS_t eflags,
                                        struct map_handler *handler);

/**
 * @brief Best-effort drop of 4KiB identity leaves over @p [phy, phy+len).
 *
 * Pair with @c map_kernel_identity_empty_range (e.g. driver teardown). Do not
 * use for @c ensure_kernel_identity_range / firmware spans. No @p eflags.
 *
 * Per page:
 * - unmapped → skip (idempotent);
 * - L3 identity → @c unmap;
 * - L2 huge or non-identity → log, remember failure, **continue** (do not
 *   panic; still clean other 4KiB pages).
 *
 * On partial failure, as many L3 leaves as possible are gone; the next
 * @c map_kernel_identity_empty_range on the same span will fail until reboot
 * or manual repair — that is intentional.
 *
 * @return @c REND_SUCCESS if every covered page was already unmapped or
 *         dropped as L3 identity; otherwise a negative @c error_t after still
 *         attempting to clean the rest of the span. Never panics.
 */
error_t unmap_kernel_identity_range(struct VSpace *vs, paddr phy, size_t len,
                                    struct map_handler *handler);

#endif /* _RENDEZVOS_MM_KERNEL_UTILS_H_ */
