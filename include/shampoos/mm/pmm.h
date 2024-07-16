#ifndef _SHAMPOOS_PMM_H_
#define _SHAMPOOS_PMM_H_

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

#include <shampoos/limits.h>

struct region {
	u64 addr;
	u64 len;
};

#define PPN(addr) (addr >> 12)
#define IDX_FROM_PPN(order, ppn) (ppn >> order)
#define PPN_FROM_IDX(order, idx) (idx << order)
#define BUCKET_FRAME_FROM_PPN(bucket, ppn) (bucket.pages[IDX_FROM_PPN(ppn)])

typedef u64 addr_t;
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
	u64 zone_upper_addr;
	u64 zone_lower_addr;
	int zone_total_pages;
	int zone_total_avaliable_pages;
	struct page_frame avaliable_zone_head[BUDDY_MAXORDER + 1];
	struct page_frame *zone_head_frame[BUDDY_MAXORDER + 1];
};
enum zone_type {
	ZONE_NORMAL,
	ZONE_NR_MAX
};

struct pmm {
	// region_count record continuous memory regions number
	int region_count;
	// memory_regions record the memory regions
	struct region memory_regions[SHAMPOOS_MAX_MEMORY_REGIONS];
	// buckets record the number of the buckets
	struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
	// zone record the number of the zone
	struct buddy_zone zone[ZONE_NR_MAX];
	u64 avaliable_phy_addr_end;
	// init buddy pmm
	void (*pmm_init)(struct setup_info *arch_setup_info);
	// alloc one page
	u32 (*pmm_alloc)(size_t page_number);
	// free one page
	int (*pmm_free)(u32 ppn, size_t page_number);
};
#define GET_AVALI_HEAD_PTR(zone_n, order)                                      \
	(&(buddy_pmm.zone[zone_n].avaliable_zone_head[order]))
#define GET_HEAD_PTR(zone_n, order)                                            \
	(buddy_pmm.zone[zone_n].zone_head_frame[order])

static inline void __frame_list_add(struct page_frame *new_node,
									struct page_frame *prev,
									struct page_frame *next) {
	next->prev = KERNEL_VIRT_TO_PHY((u64)new_node);
	new_node->next = KERNEL_VIRT_TO_PHY((u64)next);
	new_node->prev = KERNEL_VIRT_TO_PHY((u64)prev);
	prev->next = KERNEL_VIRT_TO_PHY((u64)new_node);
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
	prev->next = KERNEL_VIRT_TO_PHY((u64)next);
	next->prev = KERNEL_VIRT_TO_PHY((u64)prev);
}
/*here our module shoule node indepedent of the whole kernel,so we cannot
 * realize the list_del in linux*/
static inline void frame_list_del_init(struct page_frame *node) {
	__frame_list_del((struct page_frame *)KERNEL_PHY_TO_VIRT(node->prev),
					 (struct page_frame *)KERNEL_PHY_TO_VIRT(node->next));
	node->prev = KERNEL_VIRT_TO_PHY((u64)node);
	node->next = KERNEL_VIRT_TO_PHY((u64)node);
}
static inline void frame_list_del(struct page_frame *node) {
	frame_list_del_init(node);
}
static inline bool frame_list_empty(struct page_frame *head) {
	u64 next_virt_addr = KERNEL_PHY_TO_VIRT((u64)(head->next));
	u64 prev_virt_addr = KERNEL_PHY_TO_VIRT((u64)(head->prev));
	if (((u64)head) == next_virt_addr && ((u64)head) == prev_virt_addr)
		return true;
	return false;
}

void calculate_bucket_space();
void calculate_avaliable_phy_addr_end();
void generate_buddy_bucket(u64 kernel_phy_start, u64 kernel_phy_end,
						   u64 buddy_phy_start, u64 buddy_phy_end);

#endif
