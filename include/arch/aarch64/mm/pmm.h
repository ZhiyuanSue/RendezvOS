#ifndef _SHAMPOOS_ARCH_PMM_H_
#define _SHAMPOOS_ARCH_PMM_H_
#ifndef _SHAMPOOS_KERNEL_OFFSET_
#define _SHAMPOOS_KERNEL_OFFSET_ 0xffffffffc0000000

#ifndef KERNEL_VIRT_OFFSET
#define KERNEL_VIRT_OFFSET 0xffffffffc0000000
#endif

#define KERNEL_VIRT_OFFSET_MASK	(~KERNEL_VIRT_OFFSET)
#define KERNEL_PHY_TO_VIRT(phy_addr) (phy_addr+_SHAMPOOS_KERNEL_OFFSET_)
#define KERNEL_VIRT_TO_PHY(virt_addr) (virt_addr-_SHAMPOOS_KERNEL_OFFSET_)
#endif

#include <arch/aarch64/boot/arch_setup.h>

void arch_init_pmm(struct setup_info* arch_setup_info);
#endif