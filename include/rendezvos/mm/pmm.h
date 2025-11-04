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
#include <rendezvos/smp/percpu.h>

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

#define PMM_COMMON                                                     \
        void (*pmm_init)();                                            \
        i64 (*pmm_alloc)(size_t page_number,                           \
                         enum zone_type zone_number,                   \
                         size_t * alloced_page_number);                \
        error_t (*pmm_free)(i64 ppn, size_t page_number);              \
        size_t (*pmm_calculate_manage_space)(size_t zone_page_number); \
        spin_lock spin_ptr

extern struct spin_lock_t pmm_spin_lock;
struct pmm {
        PMM_COMMON;
};

typedef struct {
#define PAGE_FRAME_ALLOCED   (1 << 0)
#define PAGE_FRAME_AVALIABLE (1 << 1)
        u32 flags : 4;
        u32 ref_count;
        void* rmap_list;
} Page;
typedef struct {
        struct list_entry section_list;
        size_t page_count;
        Page pages[];
} MemSection;
static inline Page* get_Section_Page_from_index(MemSection* sec, size_t index)
{
        if (index < sec->page_count) {
                return &(sec->pages[index]);
        }
        return NULL;
}

typedef struct {
        struct list_entry section_list;
        struct pmm* pmm;
        paddr zone_upper_addr;
        paddr zone_lower_addr;
        size_t zone_total_pages;
        size_t zone_total_sections;
        size_t zone_total_avaliable_pages;
        size_t zone_pmm_manage_pages;
} MemZone;

extern MemZone mem_zones[ZONE_NR_MAX];
// get the pages pmm manager need
void calculate_avaliable_phy_addr_region(paddr* avaliable_phy_addr_start,
                                         paddr* avaliable_phy_addr_end,
                                         size_t* total_phy_page_frame_number);
void split_pmm_zones(paddr lower, paddr upper, size_t* total_section_number);
size_t calculate_sec_and_page_frame_pages(size_t total_phy_page_frame_number,
                                          size_t total_section_number);
void calculate_pmm_space(u64* total_pages, u64* L2_table_pages);
void generate_pmm_data(paddr pmm_data_phy_start, paddr pmm_data_phy_end);
void clean_pmm_region(paddr pmm_data_phy_start, paddr pmm_data_phy_end);

// map the percpu data
void arch_map_percpu_data_space(paddr prev_region_phy_end,
                                paddr percpu_phy_start, paddr percpu_phy_end);
// map the pmm manage data
void arch_map_pmm_data_space(paddr kernel_phy_end, paddr extra_data_phy_start,
                             paddr extra_data_phy_end, paddr pmm_l2_start,
                             u64 pmm_l2_pages);
#endif
