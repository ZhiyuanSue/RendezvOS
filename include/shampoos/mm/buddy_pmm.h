#ifndef _SHAMPOOS_BUDDY_PMM_H_
#define _SHAMPOOS_BUDDY_PMM_H_

#include "pmm.h"

#define IDX_FROM_PPN(order, ppn) (ppn >> order)
#define PPN_FROM_IDX(order, idx) (idx << order)
#define BUCKET_FRAME_FROM_PPN(bucket, ppn) (bucket.pages[IDX_FROM_PPN(ppn)])

#define BUDDY_MAXORDER 9
/*for buddy in linux, this number is 10, but I think the page table will map a
 * 2Mb page, which need the order 9*/

struct page_frame {
#define PAGE_FRAME_ALLOCED (1 << 0)
#define PAGE_FRAME_AVALIABLE (1 << 1)
	u64 flags : 2;
	u64 shared_count : 6;
	u64 prev : 28;
	u64 next : 28;
} __attribute__((packed));

struct buddy_bucket {
	u64 order;
	struct page_frame *pages;
};

struct buddy_zone {
	paddr zone_upper_addr;
	paddr zone_lower_addr;
	int zone_total_pages;
	int zone_total_avaliable_pages;
	struct page_frame *avaliable_frame[BUDDY_MAXORDER + 1];
	struct page_frame *zone_head_frame[BUDDY_MAXORDER + 1];
};

enum zone_type {
	ZONE_NORMAL,
	ZONE_NR_MAX
};

struct buddy {
	PMM_COMMON;
	struct memory_regions *m_regions;

	// buckets record the number of the buckets
	struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
	// zone record the number of the zone
	struct buddy_zone zone[ZONE_NR_MAX];
	paddr buddy_phy_start_addr;
	paddr avaliable_phy_addr_end;
};
#define GET_AVALI_HEAD_PTR(zone_n, order)                                      \
	(buddy_pmm.zone[zone_n].avaliable_frame[order])
#define GET_HEAD_PTR(zone_n, order)                                            \
	(buddy_pmm.zone[zone_n].zone_head_frame[order])

static inline void __frame_list_add(struct page_frame *bucket_head,
									struct page_frame *new_node,
									struct page_frame *prev,
									struct page_frame *next) {
	next->prev = new_node - bucket_head;
	new_node->next = next - bucket_head;
	new_node->prev = prev - bucket_head;
	prev->next = new_node - bucket_head;
}
static inline void frame_list_add_head(struct page_frame *bucket_head,
									   struct page_frame *new_node,
									   struct page_frame *head) {
	__frame_list_add(bucket_head, new_node, head, bucket_head + head->next);
}
static inline void frame_list_add_tail(struct page_frame *bucket_head,
									   struct page_frame *new_node,
									   struct page_frame *head) {
	__frame_list_add(bucket_head, new_node, bucket_head + head->prev, head);
}
static inline void __frame_list_del(struct page_frame *bucket_head,
									struct page_frame *prev,
									struct page_frame *next) {
	prev->next = next - bucket_head;
	next->prev = prev - bucket_head;
}
/*here our module shoule node indepedent of the whole kernel,so we cannot
 * realize the list_del in linux*/
static inline void frame_list_del_init(struct page_frame *bucket_head,
									   struct page_frame *node) {
	__frame_list_del(bucket_head, bucket_head + node->prev,
					 bucket_head + node->next);
	node->prev = node - bucket_head;
	node->next = node - bucket_head;
}
static inline void frame_list_del(struct page_frame *bucket_head,
								  struct page_frame *node) {
	frame_list_del_init(bucket_head, node);
}
static inline bool frame_list_only_one(struct page_frame *bucket_head,
									   struct page_frame *head) {
	struct page_frame *next = bucket_head + head->next;
	struct page_frame *prev = bucket_head + head->prev;
	if (head == next && head == prev)
		return true;
	return false;
}

void calculate_bucket_space();
void calculate_avaliable_phy_addr_end();
void generate_buddy_bucket(paddr kernel_phy_start, paddr kernel_phy_end,
						   paddr buddy_phy_start, paddr buddy_phy_end);

#endif