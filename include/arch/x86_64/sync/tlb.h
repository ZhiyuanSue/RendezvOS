#ifndef _SHAMPOOS_TLB_H_
#define _SHAMPOOS_TLB_H_
#include <common/types.h>
#include <common/mm.h>
static inline void invlpg(vaddr addr)
{
        asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

static inline void arch_tlb_invalidate(vaddr addr)
{
        invlpg(addr);
}

static inline void arch_tlb_invalidate_range(vaddr start, vaddr end)
{
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                invlpg(addr);
        }
}

#endif