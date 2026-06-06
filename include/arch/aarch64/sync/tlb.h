#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_

#include <common/types.h>
#include <common/mm.h>
#include "barrier.h"
static inline void arch_tlb_invalidate_all(void)
{
        dsb(SY);
        __asm__ __volatile__("tlbi vmalle1;");
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_page(u64 asid, vaddr addr)
{
        u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(SY);
        __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_page_all_core(u64 asid, vaddr addr)
{
        u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(SY);
        __asm__ __volatile__("tlbi vae1is,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        u64 tmp = ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(SY);
        __asm__ __volatile__("tlbi vaale1,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_kernel_page_all_core(vaddr addr)
{
        u64 tmp = ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(SY);
        __asm__ __volatile__("tlbi vaale1is,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_vspace_page(u64 asid, vaddr addr)
{
        (void)addr;
        if (asid >= (1 << 16))
                return;
        u64 tmp = (asid << 48);
        dsb(SY);
        __asm__ __volatile__("tlbi aside1, %0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_vspace_page_all_core(u64 asid, vaddr addr)
{
        (void)addr;
        if (asid >= (1 << 16))
                return;
        u64 tmp = (asid << 48);
        dsb(SY);
        __asm__ __volatile__("tlbi aside1is, %0;" : : "r"(tmp));
        dsb(SY);
        isb();
}

static inline void arch_tlb_invalidate_range(u_int64_t asid, vaddr start,
                                             vaddr end)
{
        if (asid >= (1 << 16))
                return;
        dsb(SY);
        /*the tlbi rvae1 is only supported when ARMv8.4 TLBI is implemented*/
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1)) ;
                __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        }
        dsb(SY);
        isb();
}
#endif