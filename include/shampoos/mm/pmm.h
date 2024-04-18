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

struct pmm_region{
	void*	start_addr;
	u64		length;
};

struct page_frame{
	u64		have_alloc;
	struct	list_entry list_node;
	u64		page_address;
}__attribute__((packed));
void pmm_init(struct setup_info* arch_setup_info);

#endif
