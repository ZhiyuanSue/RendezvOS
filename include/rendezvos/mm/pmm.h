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

enum zone_type { ZONE_NORMAL, ZONE_NR_MAX };

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

static inline i64 phy_Page_ppn(Page* page)
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
        u64 total_avaliable_pages;

struct pmm {
        PMM_COMMON;
};

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

error_t phy_mm_init(struct setup_info* arch_setup_info);

#endif
