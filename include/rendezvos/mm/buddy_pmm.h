#ifndef _RENDEZVOS_BUDDY_PMM_H_
#define _RENDEZVOS_BUDDY_PMM_H_

#include "pmm.h"
#include <common/dsa/list.h>

#define BUDDY_MAXORDER 9
/*for buddy in linux, this number is 10, but I think the page table will map a
 * 2Mb page, which need the order 9*/

struct buddy_page {
        struct list_entry page_list;
        i64 ppn;
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

struct buddy {
        PMM_COMMON;

        u64 buddy_page_number;
        struct buddy_page *pages;
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
};

#endif