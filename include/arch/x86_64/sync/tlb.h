#ifndef _RENDEZVOS_TLB_H_
#define _RENDEZVOS_TLB_H_
#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/tlb_cpu_mask.h>

/**
 * @brief Invalidate one linear page in the current CPU's TLB.
 */
static inline void invlpg(vaddr addr)
{
        __asm__ __volatile__("invlpg (%0)" ::"r"(addr) : "memory");
}

/**
 * @brief Flush current CPU user TLB by reloading CR3.
 */
static inline void arch_tlb_invalidate_all(void)
{
        u64 cr3_val;
        __asm__ __volatile__("mov %%cr3, %0\n\t"
                             "mov %0, %%cr3"
                             : "=r"(cr3_val)
                             :
                             : "memory");
}

/**
 * @brief Local invalidate of one VA
 */
static inline void arch_tlb_invalidate_page(u64 asid, vaddr addr)
{
        (void)asid;
        invlpg(addr);
}

/**
 * @brief Soft-IPI remote @c invlpg for CPUs in @p cpu_mask (plus local flush).
 *
 * @param addr Virtual address for invlpg
 * @param cpu_mask VSpace tlb_cpu_mask (NULL means local only)
 */
void arch_smp_flush_page_tlb(vaddr addr, const vs_tlb_cpu_bitmap_t *cpu_mask);

/**
 * @brief Soft-IPI full TLB flush (CR3 reload) for CPUs in @p cpu_mask.
 */
void arch_smp_flush_all_tlb(const vs_tlb_cpu_bitmap_t *cpu_mask);

/**
 * @brief Init this CPU's ipi data and register IPI handler once.
 */
void arch_smp_flush_tlb_init(void);

/**
 * @brief using IPI to flush one user/kernel VA according to @p cpu_mask.
 */
static inline void
arch_tlb_invalidate_page_all_core(u64 asid, vaddr addr,
                                  const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        (void)asid;
        arch_smp_flush_page_tlb(addr, cpu_mask);
}

/**
 * @brief Local invlpg for a kernel VA.
 */
static inline void arch_tlb_invalidate_kernel_page(vaddr addr)
{
        invlpg(addr);
}
/**
 * @brief For Kernel pages, the RendezvOS kmalloc is designed percpu.
 * So we only need to invlpg of current core map handler.
 *
 * And if it's not percpu, It might have huge problem. As ipi can be ignored by
 * `cli`. So if core A try to unmap, and core B is in a `cli` range and cannot
 * get the interrupt immediately, core B might still try to access this page, so
 * old translate might be used and the unmapped page have been change. If this
 * page is re-allocated and mapped, another core will find this page is changing.
 *
 * But the kmalloc's percpu strategy will let it work.
 *
 * It doesn't mean core B cannot access a page that hold by core A.
 * It can access a kernel object using IPC, which might send a pointer to core
 * B. The reason is that if a page need to unmap by core A, no any core B will
 * hold it, it's promised by kmalloc, a MSQ is used for return the kernel object
 * to the core that alloc it. So this percpu is used just for emmmm, lifecycle.
 * Only the core that alloc this page can free it after it's sure no other core
 * hold it.
 */
static inline void arch_tlb_invalidate_kernel_page_all_core(vaddr addr)
{
        invlpg(addr);
}

/**
 * @brief Local full TLB flush (reloads CR3)
 */
static inline void arch_tlb_invalidate_vspace_page(u64 asid, vaddr addr)
{
        (void)asid;
        (void)addr;
        arch_tlb_invalidate_all();
}

/**
 * @brief IPI all-TLB flush for CPUs in @p cpu_mask
 */
static inline void
arch_tlb_invalidate_vspace_page_all_core(u64 asid, vaddr addr,
                                         const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        (void)asid;
        (void)addr;
        arch_smp_flush_all_tlb(cpu_mask);
}

/**
 * @brief Local invlpg for each page in [start, end).
 */
static inline void arch_tlb_invalidate_range(u64 asid, vaddr start, vaddr end)
{
        (void)asid;
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                invlpg(addr);
        }
}

#endif