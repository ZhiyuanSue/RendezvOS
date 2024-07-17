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

#define PMM_COMMON                                                             \
	void (*pmm_init)(struct setup_info * arch_setup_info);                     \
	u32 (*pmm_alloc)(size_t page_number);                                      \
	int (*pmm_free)(u32 ppn, size_t page_number)

struct pmm {
	PMM_COMMON;
};

#endif
