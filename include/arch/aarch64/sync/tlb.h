#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_

#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/tlb_cpu_mask.h>
#include "barrier.h"

/**
 * @brief Local EL1 full invalidate
 */
static inline void arch_tlb_invalidate_all(void)
{
        dsb(ISHST);
        __asm__ __volatile__("tlbi vmalle1;");
        dsb(ISH);
        isb();
}

/**
 * @brief Local invalidate VA+ASID
 */
static inline void arch_tlb_invalidate_page(u64 asid, vaddr addr)
{
        u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(ISHST);
        __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Broadcast invalidate VA+ASID; @p cpu_mask unused.
 */
static inline void
arch_tlb_invalidate_page_all_core(u64 asid, vaddr addr,
                                  const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1));
        (void)cpu_mask;
        dsb(ISHST);
        __asm__ __volatile__("tlbi vae1is,%0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Local kernel VA any-ASID
 */
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        u64 tmp = ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(ISHST);
        __asm__ __volatile__("tlbi vaale1,%0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Broadcast kernel VA any-ASID
 */
static inline void arch_tlb_invalidate_kernel_page_all_core(vaddr addr)
{
        u64 tmp = ((addr >> 12) & ((1ULL << 44) - 1));
        dsb(ISHST);
        __asm__ __volatile__("tlbi vaale1is,%0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Local invalidate by ASID; @p addr unused.
 * @note No-op if @p asid >= 2^16.
 */
static inline void arch_tlb_invalidate_vspace_page(u64 asid, vaddr addr)
{
        (void)addr;
        if (asid >= (1 << 16))
                return;
        u64 tmp = (asid << 48);
        dsb(ISHST);
        __asm__ __volatile__("tlbi aside1, %0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Broadcast invalidate by ASID; @p cpu_mask unused.
 */
static inline void
arch_tlb_invalidate_vspace_page_all_core(u64 asid, vaddr addr,
                                         const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        (void)addr;
        (void)cpu_mask;
        if (asid >= (1 << 16))
                return;
        u64 tmp = (asid << 48);
        dsb(ISHST);
        __asm__ __volatile__("tlbi aside1is, %0;" : : "r"(tmp));
        dsb(ISH);
        isb();
}

/**
 * @brief Local per-page @c tlbi vae1 over [start, end) (no ARMv8.4 range op support case).
 */
static inline void arch_tlb_invalidate_range(u_int64_t asid, vaddr start,
                                             vaddr end)
{
        if (asid >= (1 << 16))
                return;
        dsb(ISHST);
        /*the tlbi rvae1 is only supported when ARMv8.4 TLBI is implemented*/
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                u64 tmp = (asid << 48) | ((addr >> 12) & ((1ULL << 44) - 1));
                __asm__ __volatile__("tlbi vae1,%0;" : : "r"(tmp));
        }
        dsb(ISH);
        isb();
}
#endif