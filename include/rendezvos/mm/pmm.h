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
        i64 ref_count;
        void* rmap_list;
        MemSection* sec;
} Page;
struct mem_section {
        struct list_entry section_list;
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
static inline bool ppn_in_Sec(MemSection* sec, i64 ppn)
{
        if (ppn <= 0)
                return false;
        return PADDR(ppn) > sec->lower_addr && PADDR(ppn) < sec->upper_addr;
}
static inline Page* Sec_phy_Page(MemSection* sec, size_t index)
{
        if (sec && index < sec->page_count) {
                return &(sec->pages[index]);
        }
        return NULL;
}

typedef struct {
        struct list_entry section_list;
        struct pmm* pmm;
        paddr upper_addr;
        paddr lower_addr;
        size_t zone_total_pages;
        size_t zone_total_sections;
        size_t zone_pmm_manage_pages;
} MemZone;

#define for_each_sec_of_zone(zone_ptr)                                       \
        for (MemSection* sec = container_of(                                 \
                     zone_ptr->section_list.next, MemSection, section_list); \
             sec != (MemSection*)zone_ptr;                                   \
             sec = container_of(                                             \
                     sec->section_list.next, MemSection, section_list))

static inline bool ppn_in_Zone(MemZone* zone, i64 ppn)
{
        if (ppn <= 0)
                return false;
        return PADDR(ppn) > zone->lower_addr && PADDR(ppn) < zone->upper_addr;
}
static inline Page* Zone_phy_Page(MemZone* zone, size_t index)
{
        if (zone && index < zone->zone_total_pages) {
                struct list_entry* list_node = zone->section_list.next;
                while (list_node != &zone->section_list) {
                        MemSection* sec = container_of(
                                list_node, MemSection, section_list);
                        if (index < sec->page_count) {
                                return Sec_phy_Page(sec, index);
                        } else {
                                index -= sec->page_count;
                        }
                        list_node = list_node->next;
                }
        }
        return NULL;
}
static inline Page* ppn_Zone_phy_Page(MemZone* zone, i64 ppn)
{
        if (ppn <= 0) { /*invalid ppn value*/
                return NULL;
        }
        if (!ppn_in_Zone(zone, ppn))
                return NULL;
        for_each_sec_of_zone(zone)
        {
                if (ppn_in_Sec(sec, ppn)) {
                        return &(sec->pages[ppn - PPN(sec->lower_addr)]);
                }
        }
        return NULL;
}
static inline i64 ppn_Zone_index(MemZone* zone, i64 ppn)
{
        if (ppn <= 0) { /*invalid ppn value*/
                return -1;
        }
        if (!ppn_in_Zone(zone, ppn))
                return -1;
        i64 index = 0;
        for_each_sec_of_zone(zone)
        {
                if (ppn_in_Sec(sec, ppn)) {
                        return index + ppn - PPN(sec->lower_addr);
                } else {
                        index += PPN(sec->upper_addr - sec->lower_addr
                                     - PAGE_SIZE);
                }
        }
        return (-1);
}

#define PMM_COMMON                                                          \
        void (*pmm_init)(struct pmm * pmm,                                  \
                         paddr pmm_phy_start_addr,                          \
                         paddr pmm_phy_end_addr);                           \
        i64 (*pmm_alloc)(struct pmm * pmm,                                  \
                         size_t page_number,                                \
                         size_t* alloced_page_number);                      \
        error_t (*pmm_free)(struct pmm * pmm, i64 ppn, size_t page_number); \
        size_t (*pmm_calculate_manage_space)(size_t zone_page_number);      \
        void (*pmm_show_info)(struct pmm * pmm);                            \
        spin_lock spin_ptr;                                                 \
        MemZone* zone;

extern struct spin_lock_t pmm_spin_lock;
struct pmm {
        PMM_COMMON;
};

extern MemZone mem_zones[ZONE_NR_MAX];

error_t phy_mm_init(struct setup_info* arch_setup_info);

#endif
