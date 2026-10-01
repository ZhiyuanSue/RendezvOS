/* As for pmm module,some are related to the platform, but some are not
 * So I easyly split it into two part that arch relative and no relative
 * */
#ifndef _RENDEZVOS_ARCH_PMM_H_
#define _RENDEZVOS_ARCH_PMM_H_

#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>

#ifdef KERNEL_VIRT_OFFSET

#define KERNEL_VIRT_OFFSET_MASK   (~KERNEL_VIRT_OFFSET)
#define KERNEL_PHY_TO_VIRT(paddr) ((paddr) + KERNEL_VIRT_OFFSET)
#define KERNEL_VIRT_TO_PHY(vaddr) ((vaddr) - KERNEL_VIRT_OFFSET)
#else
#error "A KERNEL_VIRT_OFFSET micro must be defined"
#endif

/**
 * @brief get memory info from multiboot into @c m_regions.
 *
 * @param arch_setup_info Boot setup info including Multiboot fields.
 * @param next_region_phy_start Output physical start for next region.
 *
 * @note if no usable memory regions, run @c kernel_halt() and does not return.
 */
void arch_init_pmm(struct setup_info *arch_setup_info,
                   vaddr *next_region_phy_start);

#endif
