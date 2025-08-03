#ifndef _RENDEZVOS_BUDDY_PMM_H_
#define _RENDEZVOS_BUDDY_PMM_H_

#include "pmm.h"
#include <common/dsa/list.h>

#define BUDDY_MAXORDER 9
/*for buddy in linux, this number is 10, but I think the page table will map a
 * 2Mb page, which need the order 9*/

#define IDX_FROM_PPN(order, ppn)           ((u64)ppn >> order)
#define PPN_FROM_IDX(order, idx)           (idx << order)
#define BUCKET_FRAME_FROM_PPN(bucket, ppn) (bucket.pages[IDX_FROM_PPN(ppn)])

struct page_frame {
#define PAGE_FRAME_ALLOCED   (1 << 0)
#define PAGE_FRAME_AVALIABLE (1 << 1)
        u32 flags;
        u32 ref_count;
        u64 index;
        struct list_entry page_list;
};

struct buddy_bucket {
        u64 order;
        struct page_frame *pages;
};

struct buddy_zone {
        paddr zone_upper_addr;
        paddr zone_lower_addr;
        int zone_total_pages;
        int zone_total_avaliable_pages;
        struct page_frame avaliable_frame[BUDDY_MAXORDER + 1];
        struct page_frame *zone_head_frame[BUDDY_MAXORDER + 1];
};

struct buddy {
        PMM_COMMON;
        struct memory_regions *m_regions;

        // buckets record the number of the buckets
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
        // zone record the number of the zone
        struct buddy_zone zone[ZONE_NR_MAX];
        paddr avaliable_phy_addr_end;
};
#define GET_AVALI_HEAD_PTR(zone_n, order) \
        (buddy_pmm.zone[zone_n].avaliable_frame[order])
#define GET_HEAD_PTR(zone_n, order) \
        (buddy_pmm.zone[zone_n].zone_head_frame[order])
#define GET_ORDER_PAGES(order) (buddy_pmm.buckets[order].pages)

// get the pages pmm manager need
u64 calculate_pmm_space(void);
void generate_pmm_data(paddr pmm_data_phy_start, paddr pmm_data_phy_end);

#endif