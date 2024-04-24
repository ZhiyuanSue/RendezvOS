#ifndef _SHAMPOOS_PMM_H_
#define _SHAMPOOS_PMM_H_

#include <shampoos/types.h>
#include <shampoos/stddef.h>
#include <shampoos/list.h>

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

#define PAGE_SIZE	0x1000
#define MIDDLE_PAGE_SIZE	0x200000
#define HUGE_PAGE_SIZE	0x40000000

#define	KiloBytes	0x400
#define	MegaBytes	0x100000
#define	GigaBytes	0x40000000

#define ROUND_UP(x,align)	((x+(align-1)) & ~(align-1))
#define ROUND_DOWN(x,align)	(x & ~(align-1))

#define	ALIGNED(x,align)	((x & (align-1))==0)

#define	PPN(addr)	(addr>>12)
#define IDX_FROM_PPN(order,ppn)	(ppn>>order)
#define	PPN_FROM_IDX(order,idx)	(idx<<order)
#define BUCKET_FRAME_FROM_PPN(bucket,ppn)	(bucket.pages[IDX_FROM_PPN(ppn)])

typedef u64 addr_t;
#define BUDDY_MAXORDER	9
/*for buddy in linux, this number is 10, but I think the page table will map a 2Mb page, which need the order 9*/

struct page_frame{
#define	PAGE_FRAME_ALLOCED	(1<<0)
#define	PAGE_FRAME_AVALIABLE	(1<<1)
	u64		flags:4;
	u64		prev:30;
	u64		next:30;
	/*
	* the kernel data must in 1G field, so 30 bit plus the kernel_virt_offset is necessary
	*/
}__attribute__((packed));

struct buddy_bucket{
	u64		order;
	struct	page_frame*	pages;
};

struct buddy_zone{
	u64		zone_upper_addr;
	u64		zone_lower_addr;
	struct page_frame avaliable_zone_head[BUDDY_MAXORDER+1];
	struct page_frame*	zone_head_frame[BUDDY_MAXORDER+1];
};
enum zone_type{
	ZONE_NORMAL,
	ZONE_NR_MAX
};

struct pmm{
	struct	buddy_bucket buckets[BUDDY_MAXORDER+1];
	struct	buddy_zone	zone[ZONE_NR_MAX];
	u64	avaliable_phy_addr_end;
	void (*pmm_init)(struct setup_info* arch_setup_info);
	u32	(*pmm_alloc)(size_t page_number);
	u64 (*pmm_free_one)(u32 ppn);
	u64 (*pmm_free)(u32 ppn,size_t page_number);
};

void pmm_init(struct setup_info* arch_setup_info);
u32	pmm_alloc(size_t page_number);
u64 pmm_free_one(u32 ppn);
u64 pmm_free(u32 ppn,size_t page_number);

static inline void __frame_list_add(struct page_frame *new_node,
		struct page_frame *prev,
		struct page_frame *next)
{
	next->prev=KERNEL_VIRT_TO_PHY((u64)new_node);
	new_node->next=KERNEL_VIRT_TO_PHY((u64)next);
	new_node->prev=KERNEL_VIRT_TO_PHY((u64)prev);
	prev->next=KERNEL_VIRT_TO_PHY((u64)new_node);
}
static inline void frame_list_add_head(struct page_frame *new_node,struct page_frame *head)
{
	__frame_list_add(new_node,head,(struct page_frame*)KERNEL_PHY_TO_VIRT(head->next));
}
static inline void frame_list_add_tail(struct page_frame *new_node,struct page_frame *head)
{
	__frame_list_add(new_node,(struct page_frame*)KERNEL_PHY_TO_VIRT(head->prev),head);
}
static inline void __frame_list_del(struct page_frame *prev,struct page_frame *next)
{
	prev->next=KERNEL_VIRT_TO_PHY((u64)next);
	next->prev=KERNEL_VIRT_TO_PHY((u64)prev);
}
/*here our module shoule node indepedent of the whole kernel,so we cannot realize the list_del in linux*/
static inline void frame_list_del_init(struct page_frame *node)
{
	__frame_list_del((struct page_frame*)KERNEL_PHY_TO_VIRT(node->prev),(struct page_frame*)KERNEL_PHY_TO_VIRT(node->next));
	node->prev=KERNEL_VIRT_TO_PHY((u64)node);
	node->next=KERNEL_VIRT_TO_PHY((u64)node);
}
static inline void frame_list_del(struct page_frame *node)
{
	frame_list_del_init(node);
}

#endif
