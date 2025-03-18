#ifndef _RENDEZVOS_ARCH_PMM_H_
#define _RENDEZVOS_ARCH_PMM_H_

#include <arch/aarch64/boot/arch_setup.h>
#ifdef KERNEL_VIRT_OFFSET

#define KERNEL_VIRT_OFFSET_MASK       (~KERNEL_VIRT_OFFSET)
#define KERNEL_PHY_TO_VIRT(phy_addr)  (phy_addr + KERNEL_VIRT_OFFSET)
#define KERNEL_VIRT_TO_PHY(virt_addr) (virt_addr - KERNEL_VIRT_OFFSET)
#else
#error "A KERNEL_VIRT_OFFSET micro must be defined"
#endif

void arch_init_pmm(struct setup_info *arch_setup_info);
#endif