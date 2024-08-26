/* As for pmm module,some are related to the platform, but some are not
 * So I easyly split it into two part that arch relative and no relative
 * */
#ifndef _SHAMPOOS_ARCH_PMM_H_
#define _SHAMPOOS_ARCH_PMM_H_

#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>

#ifdef KERNEL_VIRT_OFFSET

#define KERNEL_VIRT_OFFSET_MASK (~KERNEL_VIRT_OFFSET)
#define KERNEL_PHY_TO_VIRT(phy_addr) (phy_addr + KERNEL_VIRT_OFFSET)
#define KERNEL_VIRT_TO_PHY(virt_addr) (virt_addr - KERNEL_VIRT_OFFSET)
#else
#error "A KERNEL_VIRT_OFFSET micro must be defined"
#endif

void arch_init_pmm(struct setup_info *arch_setup_info);

#endif
