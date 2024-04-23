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

typedef u64 addr_t;
#define BUDDY_MAXORDER	9
/*for buddy in linux, this number is 10, but I think the page table will map a 2Mb page, which need the order 9*/

struct page_frame{
#define	PAGE_FRAME_ALLOCED	(1<<0)
#define	PAGE_FRAME_AVALIABLE	(1<<1)
	u64		flags:8;
	u64		prev_ppn:28;
	u64		next_ppn:28;
	/*
	* currently the phy addr is 40 bit width and the phy ppn is 28 bit width max
	* maybe at some case,the phy addr might larger than 1024GB,but it's too far for this kernel
	*/
}__attribute__((packed));

struct buddy_bucket{
	u64		order;
	struct	page_frame*	pages;
};

struct buddy_zone{
	u64		zone_upper_addr;
	u64		zone_lower_addr;
	struct {
		u64		head_prev_ppn;
		u64		head_next_ppn;
	} zone_head[BUDDY_MAXORDER];
};
enum zone_type{
	ZONE_NORMAL,
	ZONE_NR_MAX
};

struct pmm{
	struct	buddy_bucket buckets[BUDDY_MAXORDER];
	u64	avaliable_phy_addr_end;
	void (*pmm_init)(struct setup_info* arch_setup_info);
	u64	(*pmm_alloc)(size_t page_number);
	u64 (*pmm_free_one)(u64 page_address);
	u64 (*pmm_free)(u64 page_address,size_t page_number);
};

void pmm_init(struct setup_info* arch_setup_info);
u64	pmm_alloc(size_t page_number);
u64 pmm_free_one(u64 page_address);
u64 pmm_free(u64 page_address,size_t page_number);

#endif
