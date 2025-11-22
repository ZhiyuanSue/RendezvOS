#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_

#include <common/types.h>
#include <common/mm.h>
#include "barrier.h"
static inline void arch_tlb_invalidate_all()
{
        dsb(SY);
        __asm__ __volatile__("tlbi alle1;");
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_page(u64 vspace_id, vaddr addr)
{
        u64 tmp = (vspace_id << 16) | (addr >> 12);
        dsb(SY);
        __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        u64 tmp = (addr >> 12);
        dsb(SY);
        __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        dsb(SY);
        isb();
}
static inline void arch_tlb_invalidate_vspace_page(u64 vspace_id, vaddr addr)
{
        (void)addr;
        if (vspace_id >= (1 << 16))
                return;
        u64 tmp = (vspace_id << 48);
        dsb(SY);
        __asm__ __volatile__("tlbi aside1, %0;" : : "r"(tmp));
        dsb(SY);
        isb();
}

static inline void arch_tlb_invalidate_range(u_int64_t vspace_id, vaddr start,
                                             vaddr end)
{
        if (vspace_id >= (1 << 16))
                return;
        dsb(SY);
        /*the tlbi rvae1 is only supported when ARMv8.4 TLBI is implemented*/
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                u64 tmp = (vspace_id << 16) | (addr >> 12);
                __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        }
        dsb(SY);
        isb();
}
#endif