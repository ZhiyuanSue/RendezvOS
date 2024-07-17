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
#define PAGE_FRAME_SHARED                                                      \
	(1 << 2) /*used to record this page frame is                               \
				shared*/
	u64 flags : 4;
	u64 prev : 30;
	u64 next : 30;
	/*
	 * the kernel data must in 1G field, so 30 bit plus the kernel_virt_offset
	 * is necessary
	 */
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
	struct page_frame avaliable_zone_head[BUDDY_MAXORDER + 1];
	struct page_frame *zone_head_frame[BUDDY_MAXORDER + 1];
};

enum zone_type {
	ZONE_NORMAL,
	ZONE_NR_MAX
};

struct buddy {
	PMM_COMMON;
	// region_count record continuous memory regions number
	int region_count;
	// memory_regions record the memory regions
	struct region memory_regions[SHAMPOOS_MAX_MEMORY_REGIONS];
	// buckets record the number of the buckets
	struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
	// zone record the number of the zone
	struct buddy_zone zone[ZONE_NR_MAX];
	paddr avaliable_phy_addr_end;
};
#define GET_AVALI_HEAD_PTR(zone_n, order)                                      \
	(&(buddy_pmm.zone[zone_n].avaliable_zone_head[order]))
#define GET_HEAD_PTR(zone_n, order)                                            \
	(buddy_pmm.zone[zone_n].zone_head_frame[order])

static inline void __frame_list_add(struct page_frame *new_node,
									struct page_frame *prev,
									struct page_frame *next) {
	next->prev = KERNEL_VIRT_TO_PHY((vaddr)new_node);
	new_node->next = KERNEL_VIRT_TO_PHY((vaddr)next);
	new_node->prev = KERNEL_VIRT_TO_PHY((vaddr)prev);
	prev->next = KERNEL_VIRT_TO_PHY((vaddr)new_node);
}
static inline void frame_list_add_head(struct page_frame *new_node,
									   struct page_frame *head) {
	__frame_list_add(new_node, head,
					 (struct page_frame *)KERNEL_PHY_TO_VIRT(head->next));
}
static inline void frame_list_add_tail(struct page_frame *new_node,
									   struct page_frame *head) {
	__frame_list_add(new_node,
					 (struct page_frame *)KERNEL_PHY_TO_VIRT(head->prev), head);
}
static inline void __frame_list_del(struct page_frame *prev,
									struct page_frame *next) {
	prev->next = KERNEL_VIRT_TO_PHY((vaddr)next);
	next->prev = KERNEL_VIRT_TO_PHY((vaddr)prev);
}
/*here our module shoule node indepedent of the whole kernel,so we cannot
 * realize the list_del in linux*/
static inline void frame_list_del_init(struct page_frame *node) {
	__frame_list_del((struct page_frame *)KERNEL_PHY_TO_VIRT(node->prev),
					 (struct page_frame *)KERNEL_PHY_TO_VIRT(node->next));
	node->prev = KERNEL_VIRT_TO_PHY((vaddr)node);
	node->next = KERNEL_VIRT_TO_PHY((vaddr)node);
}
static inline void frame_list_del(struct page_frame *node) {
	frame_list_del_init(node);
}
static inline bool frame_list_empty(struct page_frame *head) {
	vaddr next_virt_addr = KERNEL_PHY_TO_VIRT((paddr)(head->next));
	vaddr prev_virt_addr = KERNEL_PHY_TO_VIRT((paddr)(head->prev));
	if (((vaddr)head) == next_virt_addr && ((vaddr)head) == prev_virt_addr)
		return true;
	return false;
}

void calculate_bucket_space();
void calculate_avaliable_phy_addr_end();
void generate_buddy_bucket(paddr kernel_phy_start, paddr kernel_phy_end,
						   paddr buddy_phy_start, paddr buddy_phy_end);

#endif