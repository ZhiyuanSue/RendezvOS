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

typedef u64 addr_t;
#define BUDDY_MAXORDER	9
/*for buddy in linux, this number is 10, but I think the page table will map a 2Mb page, which need the order 9*/

struct page_frame{
#define	PAGE_FRAME_ALLOCED	(1<<0)
	u64		flags;
	struct	list_entry list_node;
	addr_t	page_address;
}__attribute__((packed));

struct buddy_bucket{
	u64		order;
	u64		nr_pages;
	struct page_frame*	frames;
	struct buddy_zone*	zone;
};

struct buddy_zone{
	struct	list_entry zone_node;	/*There might have more than one zone*/
	struct	buddy_bucket	buckets[BUDDY_MAXORDER];
	addr_t	low_addr;	/*record the high and low addr of this zone ,which is need by the free*/
	addr_t	high_addr;
};

void pmm_init(struct setup_info* arch_setup_info);
u64	pmm_alloc(size_t page_number);
u64 pmm_free_one(u64 page_address);
u64 pmm_free(u64 page_address,size_t page_number);

#endif
