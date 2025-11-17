#ifndef _RENDEZVOS_BUDDY_PMM_H_
#define _RENDEZVOS_BUDDY_PMM_H_

#include "pmm.h"
#include <common/dsa/list.h>

#define BUDDY_MAXORDER 9
/*for buddy in linux, this number is 10, but I think the page table will map a
 * 2Mb page, which need the order 9*/

#define IDX_FROM_PPN(order, ppn) ((u64)(ppn) >> (order))
#define PPN_FROM_IDX(order, idx) ((idx) << (order))

struct buddy_page {
        u64 order;
        i64 ppn;
        struct list_entry page_list;
};

struct buddy_bucket {
        u64 order;
        struct list_entry avaliable_frame_list;
};

struct buddy {
        PMM_COMMON;
        // struct memory_regions *m_regions;

        u64 buddy_page_number;
        u64 total_avaliable_pages;
        struct buddy_page *pages;
        // buckets record the number of the buckets
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
        // zone record the number of the zone
        // struct buddy_page avaliable_frame[BUDDY_MAXORDER + 1];
        // paddr avaliable_phy_addr_end;
};

#endif