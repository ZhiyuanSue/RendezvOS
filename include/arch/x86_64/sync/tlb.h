#ifndef _SHAMPOOS_TLB_H_
#define _SHAMPOOS_TLB_H_
#include <common/types.h>
#include <common/mm.h>
static inline void invlpg(vaddr addr)
{
	__asm__ __volatile__("invlpg (%0)" ::"r"(addr) : "memory");
}

static inline void arch_tlb_invalidate_all()
{
	__asm__ __volatile__("mov %eax,%cr3;"
                     "mov %cr3,%eax;"
                     :
                     :
                     : "a");
}

static inline void arch_tlb_invalidate_page(uint64_t vspace_id, vaddr addr)
{
        invlpg(addr);
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        invlpg(addr);
}
static inline void arch_tlb_invalidate_vspace_page(uint64_t vspace_id,
                                                   vaddr addr)
{
        // TODO:unimplemented in x86_64
}

static inline void arch_tlb_invalidate_range(uint64_t vspace_id, vaddr start,
                                             vaddr end)
{
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                invlpg(addr);
        }
}

#endif