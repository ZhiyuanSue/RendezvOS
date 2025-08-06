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
#include <rendezvos/limits.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>

struct region {
        paddr addr;
        u64 len;
};

struct memory_regions {
        // region_count record continuous memory regions number
        u64 region_count;
        // memory_regions record the memory regions
        struct region memory_regions[RENDEZVOS_MAX_MEMORY_REGIONS];
        error_t (*memory_regions_insert)(paddr addr, u64 len);
        void (*memory_regions_delete)(int index);
        bool (*memory_regions_entry_empty)(int index);
        int (*memory_regions_reserve_region)(paddr phy_start, paddr phy_end);
        void (*memory_regions_init)(struct memory_regions* m_regions);
};

enum zone_type { ZONE_NORMAL, ZONE_NR_MAX };

#define PMM_COMMON                                                        \
        void (*pmm_init)(struct setup_info * arch_setup_info);            \
        i64 (*pmm_alloc)(size_t page_number, enum zone_type zone_number); \
        error_t (*pmm_free)(i64 ppn, size_t page_number);                 \
        spin_lock spin_ptr

extern struct spin_lock_t pmm_spin_lock;
struct pmm {
        PMM_COMMON;
};

// get the pages pmm manager need
void calculate_pmm_space(u64* total_pages, u64* L2_table_pages);
void generate_pmm_data(paddr pmm_data_phy_start, paddr pmm_data_phy_end);
void clean_pmm_region(paddr pmm_data_phy_start, paddr pmm_data_phy_end);

// map the pmm manage data
void arch_map_extra_data_space(paddr kernel_phy_start, paddr kernel_phy_end,
                               paddr extra_data_phy_start,
                               paddr extra_data_phy_end, paddr pmm_l2_start,
                               u64 pmm_l2_pages);
#endif
