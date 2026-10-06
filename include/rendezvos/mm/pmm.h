#ifndef _RENDEZVOS_PMM_H_
#define _RENDEZVOS_PMM_H_

#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/types.h>

#ifdef _AARCH64_
#include <arch/aarch64/mm/pmm.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/mm/pmm.h>
#elif defined _RISCV64_
#include <arch/riscv64/mm/pmm.h>
#elif defined _X86_64_
#include <arch/x86_64/mm/pmm.h>
#else
#include <arch/x86_64/mm/pmm.h>
#endif

#include <common/mm.h>
#include <common/dsa/list.h>
#include <common/string.h>
#include <rendezvos/limits.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>

struct memory_regions {
        // region_count record continuous memory regions number
        u64 region_count;
        // memory_regions record the memory regions
        struct region memory_regions[RENDEZVOS_MAX_MEMORY_REGIONS];
        error_t (*memory_regions_insert)(paddr addr, u64 len);
        void (*memory_regions_delete)(int index);
        bool (*memory_regions_entry_empty)(int index);
        int (*memory_regions_reserve_region)(paddr phy_start, paddr phy_end);
        int (*memory_regions_reserve_region_with_length)(size_t length,
                                                         u64 start_alignment,
                                                         paddr* phy_start,
                                                         paddr* phy_end);
        void (*memory_regions_init)(struct memory_regions* m_regions);
};
extern struct memory_regions m_regions;

/**
 * Zone indices. @c ZONE_NR_MAX is the **compile-time capacity** of
 * @c mem_zones[] / @c pmm_spin_lock[] (not the number of active zones).
 * Active count is @ref nr_mem_zones (compact prefix @c [0, nr_mem_zones)).
 */
enum zone_type {
        ZONE_NORMAL = 0,
        ZONE_NR_MAX = 16,
};
/**
 * @brief Hook: fill @ref mem_zones[0 .. nr_mem_zones) before split.
 *
 * Called from @c phy_mm_init after the post-reserve available range is known,
 * before @c split_pmm_zones. Set @ref nr_mem_zones and for each active zone
 * set @c lower_addr / @c upper_addr / @c pmm (static @c struct pmm only;
 * each zone must use a distinct @c pmm instance).
 * Weak default: one @c ZONE_NORMAL covering all available RAM + buddy.
 *
 * @param avail_lo Inclusive-style window start used by default NORMAL.
 * @param avail_hi End of that available range (same convention as MemZone).
 */
void configure_pmm_zones_hook(paddr avail_lo, paddr avail_hi);

/** Active zone count in @ref mem_zones (0 .. ZONE_NR_MAX). */
extern int nr_mem_zones;

typedef struct mem_section MemSection;
typedef struct {
        struct list_entry section_list;
        u64 zone_id;
        struct pmm* pmm;
        paddr upper_addr;
        paddr lower_addr;
        size_t zone_total_pages;
        size_t zone_total_sections;
        size_t zone_pmm_manage_pages;
} MemZone;
typedef struct {
        i64 ref_count;
        MemSection* sec;
        struct list_entry rmap_list;
} Page;
static inline void phy_Page_ref_plus(Page* page)
{
        page->ref_count++;
}
static inline void phy_Page_ref_minus(Page* page)
{
        page->ref_count--;
}
static inline bool phy_Page_refed(Page* page)
{
        return page->ref_count > 0;
}
struct mem_section {
        struct list_entry section_list;
        u64 sec_id;
        MemZone* zone;
        size_t page_count;
        paddr upper_addr;
        paddr lower_addr;
        Page pages[];
};
#define for_each_page_of_sec(sec_ptr)                     \
        for (Page* page = &sec_ptr->pages[0];             \
             page < &sec_ptr->pages[sec_ptr->page_count]; \
             page++)

/**
 * @brief Physical address of @p page's first byte.
 * @return @c sec->lower_addr + PAGE_SIZE * index, or @c -E_RENDEZVOS.
 */
static inline i64 phy_Page_first_addr(Page* page)
{
        if (page && page->sec) {
                return page->sec->lower_addr
                       + PAGE_SIZE * (page - page->sec->pages);
        }
        return -E_RENDEZVOS;
}
static inline bool ppn_in_Sec(MemSection* sec, ppn_t ppn)
{
        if (invalid_ppn(ppn))
                return false;
        return PADDR(ppn) >= sec->lower_addr && PADDR(ppn) < sec->upper_addr;
}
static inline Page* Sec_phy_Page(MemSection* sec, size_t index)
{
        if (sec && index < sec->page_count) {
                return &(sec->pages[index]);
        }
        return NULL;
}

#define for_each_sec_of_zone(zone_ptr)                                       \
        for (MemSection* sec = container_of(                                 \
                     zone_ptr->section_list.next, MemSection, section_list); \
             sec != (MemSection*)zone_ptr;                                   \
             sec = container_of(                                             \
                     sec->section_list.next, MemSection, section_list))

static inline bool ppn_in_Zone(MemZone* zone, ppn_t ppn)
{
        if (invalid_ppn(ppn))
                return false;
        return PADDR(ppn) >= zone->lower_addr && PADDR(ppn) < zone->upper_addr;
}

typedef struct {
        MemZone* zone;
        MemSection* sec;
        size_t sec_index; /* index within current section */
        i64 sec_base_index; /* zone-global index of sec->pages[0] */
} ZonePageCursor;

static inline Page* zone_page_cursor_page(ZonePageCursor* cur)
{
        if (!cur || !cur->sec)
                return NULL;
        if (cur->sec_index >= cur->sec->page_count)
                return NULL;
        return &cur->sec->pages[cur->sec_index];
}

static inline i64 zone_page_cursor_index(const ZonePageCursor* cur)
{
        if (!cur || !cur->sec)
                return -1;
        return cur->sec_base_index + (i64)cur->sec_index;
}

static inline Page* zone_page_cursor_init(ZonePageCursor* cur, MemZone* zone,
                                          ppn_t start_ppn)
{
        if (!cur)
                return NULL;
        cur->zone = zone;
        cur->sec = NULL;
        cur->sec_index = 0;
        cur->sec_base_index = -1;

        if (!zone || invalid_ppn(start_ppn) || !ppn_in_Zone(zone, start_ppn))
                return NULL;

        i64 base_index = 0;
        for_each_sec_of_zone(zone)
        {
                if (!ppn_in_Sec(sec, start_ppn)) {
                        base_index += (i64)sec->page_count;
                        continue;
                }
                size_t sec_index = (size_t)(start_ppn - PPN(sec->lower_addr));
                if (sec_index >= sec->page_count)
                        return NULL;
                cur->sec = sec;
                cur->sec_index = sec_index;
                cur->sec_base_index = base_index;
                return &sec->pages[sec_index];
        }
        return NULL;
}

static inline bool zone_page_cursor_next(ZonePageCursor* cur)
{
        if (!cur || !cur->zone || !cur->sec)
                return false;

        cur->sec_index++;
        if (cur->sec_index < cur->sec->page_count)
                return true;

        struct list_entry* next_node = cur->sec->section_list.next;
        if (next_node == &cur->zone->section_list)
                return false;

        MemSection* next_sec =
                container_of(next_node, MemSection, section_list);
        cur->sec_base_index += (i64)cur->sec->page_count;
        cur->sec = next_sec;
        cur->sec_index = 0;
        return true;
}

/**
 * @brief Sync reclaim callback for a pmm zone.
 *
 * Invoked when @c pmm_alloc cannot satisfy a request. The zone PMM lock is
 * @b not held: the callback may @c pmm_free pages of this @p pmm, but must
 * @b not call @c pmm_alloc. Swap / OOM / kill policy belongs to the caller.
 *
 * @param pmm        Zone allocator that failed (do not reclaim other zones
 * here).
 * @param need_pages Page count of the failing allocation.
 * @param have_tried_attempts    The reclaim function have tried time for this
 * allocation.
 * @return @c true to ask @c pmm_alloc to retry; @c false to give up.
 *
 * @note Core also stops after @c PMM_RECLAIM_MAX_ATTEMPTS to avoid hangs. The
 * upper level(if have set the reclaim hook), must see it as this zone cannot
 * get any page anyway
 */
typedef bool (*pmm_reclaim_fn_t)(struct pmm* pmm, size_t need_pages,
                                 unsigned have_tried_attempts);
#define PMM_RECLAIM_MAX_ATTEMPTS 64u

#define PMM_COMMON                                                            \
        void (*pmm_init)(struct pmm * pmm,                                    \
                         paddr pmm_phy_start_addr,                            \
                         paddr pmm_phy_end_addr);                             \
        ppn_t (*pmm_alloc)(struct pmm * pmm,                                  \
                           size_t page_number,                                \
                           size_t* alloced_page_number);                      \
        error_t (*pmm_free)(struct pmm * pmm, ppn_t ppn, size_t page_number); \
        size_t (*pmm_calculate_manage_space)(size_t zone_page_number);        \
        void (*pmm_show_info)(struct pmm * pmm);                              \
        spin_lock spin_ptr;                                                   \
        MemZone* zone;                                                        \
        u64 total_avaliable_pages;                                            \
        pmm_reclaim_fn_t reclaim_fn;

/**
 * @brief Per-zone physical page allocator ops + state.
 * Callers pick a zone's @c pmm (usually @c mem_zones[ZONE_NORMAL].pmm );
 *
 * @par pmm_alloc
 * Request @p page_number pages; on success returns starting @c ppn_t and
 * writes the actual count to @p alloced_page_number (buddy rounds up to 2^n).
 * @c page_number == 0 → returns 0 and alloced 0 (success). Do not use
 * @c invalid_ppn() on that path: it treats 0 as invalid.
 * Other failures are negative @c error_t (@c invalid_ppn() is true):
 * @c -E_RENDEZVOS — request larger than the allocator can give, or metadata
 * broken (no reclaim);
 * @c -E_REND_AGAIN — reclaim unset or exhausted (typical OOM);
 * @c -E_REND_NO_MEM — reclaim hook returned false.
 *
 * @par pmm_free
 * Release @p page_number pages starting at @p ppn. Returns @c REND_SUCCESS
 * or a negative error_t. Buddy drops per-page refs and merges at 0; it does
 * not require @p page_number to be a prior alloc's whole span.
 *
 * @par Locking
 * Implementations take the zone MCS lock via @c pmm_lock / @c pmm_unlock;
 * @c me must be the current CPU's @c pmm_spin_lock[zone_id] slot.
 */
struct pmm {
        PMM_COMMON;
};

/**
 * @brief Install or clear the reclaim callback for @p pmm.
 * @param pmm Zone allocator (typically @c zone->pmm).
 * @param fn  Callback, or @c NULL to disable reclaim for this pmm.
 */
static inline void pmm_set_reclaim_hook(struct pmm* pmm, pmm_reclaim_fn_t fn)
{
        if (pmm)
                pmm->reclaim_fn = fn;
}

extern MemZone mem_zones[ZONE_NR_MAX];
extern struct spin_lock_t pmm_spin_lock[ZONE_NR_MAX];
static inline void pmm_lock(struct pmm* pmm)
{
        if (!pmm || !pmm->zone)
                return;
        lock_mcs(&pmm->spin_ptr, &percpu(pmm_spin_lock[pmm->zone->zone_id]));
}

static inline void pmm_unlock(struct pmm* pmm)
{
        if (!pmm || !pmm->zone)
                return;
        unlock_mcs(&pmm->spin_ptr, &percpu(pmm_spin_lock[pmm->zone->zone_id]));
}

/**
 * @brief Increase or decrease reference counts for a range of physical pages.
 * @param pmm Zone allocator owning the pages.
 * @param start_ppn Starting physical page number.
 * @param page_number Number of pages to modify.
 * @param increment @c true to increase @c ref_count, @c false to decrease.
 * @return @c REND_SUCCESS, or a negative @c error_t
 * On failure, automatically rolls back any changes made during this call.
 */
static inline error_t pmm_change_pages_ref(struct pmm* pmm, ppn_t start_ppn,
                                           size_t page_number, bool increment)
{
        if (!pmm || !pmm->zone || page_number == 0)
                return -E_IN_PARAM;
        if (!ppn_in_Zone(pmm->zone, start_ppn))
                return -E_IN_PARAM;

        ZonePageCursor cur;
        if (!zone_page_cursor_init(&cur, pmm->zone, start_ppn))
                return -E_IN_PARAM;

        pmm_lock(pmm);
        size_t i;
        for (i = 0; i < page_number; i++) {
                Page* p = zone_page_cursor_page(&cur);
                if (!p)
                        goto fail_unroll;

                if (increment)
                        phy_Page_ref_plus(p);
                else
                        phy_Page_ref_minus(p);

                if (i + 1 < page_number) {
                        if (!zone_page_cursor_next(&cur))
                                goto fail_unroll;
                }
        }
        pmm_unlock(pmm);
        return REND_SUCCESS;

fail_unroll:
        ZonePageCursor unroll_cur;
        if (zone_page_cursor_init(&unroll_cur, pmm->zone, start_ppn)) {
                for (size_t j = 0; j < i; j++) {
                        Page* p = zone_page_cursor_page(&unroll_cur);
                        if (p) {
                                if (increment)
                                        phy_Page_ref_minus(p);
                                else
                                        phy_Page_ref_plus(p);
                        }
                        if (j + 1 < i)
                                zone_page_cursor_next(&unroll_cur);
                }
        }
        pmm_unlock(pmm);
        return -E_RENDEZVOS;
}

static inline void pmm_zone_lock(MemZone* zone)
{
        if (!zone || !zone->pmm)
                return;
        lock_mcs(&zone->pmm->spin_ptr, &percpu(pmm_spin_lock[zone->zone_id]));
}

static inline void pmm_zone_unlock(MemZone* zone)
{
        if (!zone || !zone->pmm)
                return;
        unlock_mcs(&zone->pmm->spin_ptr, &percpu(pmm_spin_lock[zone->zone_id]));
}

/**
 * @brief BSP bring-up: build zones and enable physical page allocation.
 *
 * Must run before @c virt_mm_init / @c arch_start_platform. After success,
 * zone @c pmm pointers are usable for page alloc/free.
 *
 * @param arch_setup_info Boot setup struct (arch fills memmap / DTB fields).
 * @return @c REND_SUCCESS if success, or @c -E_RENDEZVOS on reserve / layout
 * failure.
 *
 * @note Fatal arch bring-up failures may @c kernel_halt and not return.
 */
error_t phy_mm_init(struct setup_info* arch_setup_info);

#endif
