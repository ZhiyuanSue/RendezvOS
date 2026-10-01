#ifndef _RENDEZVOS_BUDDY_PMM_H_
#define _RENDEZVOS_BUDDY_PMM_H_

#include "pmm.h"
#include <common/dsa/list.h>

/**
 * @brief Max buddy order (inclusive). Largest block is 2^BUDDY_MAXORDER pages
 *        (= 4 MiB when PAGE_SIZE is 4 KiB; order 10 covers a 4 MiB leaf).
 */
#define BUDDY_MAXORDER 10
/*for buddy in linux, this number is 10, but I think the page table will map a
 * 2Mb page, which need the order 9, however, I have to compat with the linux,
 * so I changed back to 10, but I'm not sure whether have any bug. */

struct buddy_page {
        struct list_entry page_list;
        ppn_t ppn;
        /*
        use order to indicate how much page is allocable,
        -1 means this page is allocated
        */
        i64 order;
};

struct buddy_bucket {
        u64 order;
        u64 aval_pages;
        struct list_entry avaliable_frame_list;
};

/**
 * @brief Default @c struct pmm implementation (buddy allocator).
 *
 * global instance @c buddy_pmm is ZONE_NORMAL's pmm.
 */
struct buddy {
        PMM_COMMON;

        u64 buddy_page_number;
        struct buddy_page *pages;
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
};

#endif