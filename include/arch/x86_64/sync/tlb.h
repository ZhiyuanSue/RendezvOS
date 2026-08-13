#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_
#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/tlb_cpu_mask.h>

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

static inline void arch_tlb_invalidate_page(u64 asid, vaddr addr)
{
        (void)asid;
        invlpg(addr);
}

/* for x86_64, we have to send IPI to flush a page's tlb */
/* flush one page's tlb on all other used cores */
void arch_smp_flush_page_tlb(vaddr addr, const vs_tlb_cpu_bitmap_t *cpu_mask);
/* flush one vspace's pages on all other used cores */
void arch_smp_flush_all_tlb(const vs_tlb_cpu_bitmap_t *cpu_mask);
/* register the IPI handler for SMP TLB flush */
void arch_smp_flush_tlb_init(void);
static inline void
arch_tlb_invalidate_page_all_core(u64 asid, vaddr addr,
                                  const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        (void)asid;
        arch_smp_flush_page_tlb(addr, cpu_mask);
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        invlpg(addr);
}
/*
 * For Kernel pages, the RendezvOS is designed percpu.
 * So we only need to invlpg of current core
 */
static inline void arch_tlb_invalidate_kernel_page_all_core(vaddr addr)
{
        invlpg(addr);
}
static inline void arch_tlb_invalidate_vspace_page(u64 asid, vaddr addr)
{
        (void)asid;
        (void)addr;
        arch_tlb_invalidate_all();
}
static inline void
arch_tlb_invalidate_vspace_page_all_core(u64 asid, vaddr addr,
                                         const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        (void)asid;
        (void)addr;
        arch_smp_flush_all_tlb(cpu_mask);
}

static inline void arch_tlb_invalidate_range(u64 asid, vaddr start, vaddr end)
{
        (void)asid;
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                invlpg(addr);
        }
}

#endif