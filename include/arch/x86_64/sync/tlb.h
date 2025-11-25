#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_
#include <common/types.h>
#include <common/mm.h>
static inline void invlpg(vaddr addr)
{
        __asm__ __volatile__("invlpg (%0)" ::"r"(addr) : "memory");
}

static inline void arch_tlb_invalidate_all(void)
{
        u64 cr3_val;
        __asm__ __volatile__("mov %%cr3, %0\n\t"
                             "mov %0, %%cr3"
                             : "=r"(cr3_val)
                             :
                             : "memory");
}

static inline void arch_tlb_invalidate_page(u64 vspace_id, vaddr addr)
{
        (void)vspace_id;
        invlpg(addr);
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        invlpg(addr);
}
static inline void arch_tlb_invalidate_vspace_page(u64 vspace_id, vaddr addr)
{
        // TODO:unimplemented in x86_64
        (void)vspace_id;
        (void)addr;
}

static inline void arch_tlb_invalidate_range(u64 vspace_id, vaddr start,
                                             vaddr end)
{
        (void)vspace_id;
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                invlpg(addr);
        }
}

#endif