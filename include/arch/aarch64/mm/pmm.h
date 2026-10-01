#ifndef _RENDEZVOS_ARCH_PMM_H_
#define _RENDEZVOS_ARCH_PMM_H_

#include <arch/aarch64/boot/arch_setup.h>
#ifdef KERNEL_VIRT_OFFSET

#define KERNEL_VIRT_OFFSET_MASK       (~KERNEL_VIRT_OFFSET)
#define KERNEL_PHY_TO_VIRT(phy_addr)  ((phy_addr) + KERNEL_VIRT_OFFSET)
#define KERNEL_VIRT_TO_PHY(virt_addr) ((virt_addr) - KERNEL_VIRT_OFFSET)
#else
#error "A KERNEL_VIRT_OFFSET micro must be defined"
#endif

/**
 * @brief Arch: parse DTB memory nodes get memory range.
 * and put the memory info into @c m_regions for next stage pmm init.
 *
 * @param arch_setup_info Must have @c boot_dtb_header_base_addr from
 * prepare_arch.
 * @param next_region_phy_start Output physical start for next region.
 *
 * @note if no usable memory regions, run @c kernel_halt() and does not return.
 */
void arch_init_pmm(struct setup_info *arch_setup_info,
                   vaddr *next_region_phy_start);
#endif