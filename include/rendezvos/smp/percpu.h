#ifndef _RENDEZVOS_PER_CPU_
#define _RENDEZVOS_PER_CPU_
#include <common/types.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/cpu_id.h>

/** Linker section for the per-CPU */
#define PER_CPU_SECTION ".percpu..data"

/**
 * @brief Place @p name in the .percpu..data section 
 *
 * Initializers apply **only** to CPU0 (linker template). Slots for CPU1…MAX-1
 * are zeroed by @c clean_per_cpu_region — not copied from the template. Each AP
 * must reinitialize any arch-private per-CPU state after the region is cleaned.
 */
#define DEFINE_PER_CPU(type, name) \
        __attribute__((section(PER_CPU_SECTION))) __typeof__(type) name

extern char _per_cpu_end, _per_cpu_start;
/**
 * @brief Virtual base of each CPU's per-CPU region (index 0 = linker section).
 */
extern u64 __per_cpu_offset[RENDEZVOS_MAX_CPU_NUMBER];

/**
 * @brief Per-CPU software id (@c DEFINE_PER_CPU); set in @c arch_start_core.
 *
 * Use @c percpu(cpu_number) / @c per_cpu(cpu_number, id)
 */
extern cpu_id_t cpu_number;
#define per_cpu_offset(x) (__per_cpu_offset[x])

/**
 * @brief Access @p var's copy on an explicit @p cpu (cross-CPU read/write).
 * Concurrent mutation of another CPU's slot needs locks / IPI discipline.
 */
#define per_cpu(var, cpu)                                                \
        (*((__typeof__(var)*)(((vaddr)(&var) - (vaddr)(&_per_cpu_start)) \
                              + __per_cpu_offset[cpu])))

/**
 * @brief Access per-cpu @p var on the current CPU.
 *
 * Requires @c arch_enable_percpu() already done on this CPU; otherwise the
 * arch per-CPU base is wrong and silent corruption is likely.
 */
#define percpu(var)                                                      \
        (*((__typeof__(var)*)(((vaddr)(&var) - (vaddr)(&_per_cpu_start)) \
                              + get_per_cpu_base())))

/**
 * @brief Install this CPU's per-CPU virtual base into the arch register.
 *
 * After this returns, @c percpu() is valid on this CPU. 
 * 
 * @param cpu_id Index into @c __per_cpu_offset (APIC id or dense id)
 */
void arch_enable_percpu(cpu_id_t cpu_id);

/**
 * @brief Read the current CPU's per-CPU base from the arch register.
 * @return Virtual address matching @c __per_cpu_offset [ this cpu ] after enable
 */
vaddr get_per_cpu_base();

/**
 * @brief Carve per-CPU physical space after the kernel image.
 *
 * Sets @c __per_cpu_offset[0] to the linker template; if MAX>1, reserves
 * space for CPU1…MAX-1 and advances *@p phy_kernel_end accordingly.
 * Called during @c phy_mm_init.
 *
 * @param phy_kernel_end In/out: physical end cursor after the kernel
 */
void reserve_per_cpu_region(paddr* phy_kernel_end);

/**
 * @brief Fill remaining @c __per_cpu_offset[] entries from the CPU1 base.
 */
void calculate_per_cpu_offset();

/**
 * @brief Zero the reserved region for CPU1…MAX-1 (not a template copy).
 *
 * @param per_cpu_phy_addr Physical start of the extra per-CPU area (CPU1 base)
 */
void clean_per_cpu_region(paddr per_cpu_phy_addr);
#endif
