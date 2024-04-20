/* As for pmm module,some are related to the platform, but some are not
 * So I easyly split it into two part that arch relative and no relative
 * */
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

#define PAGE_SIZE	0x1000
#define MIDDLE_PAGE_SIZE	0x200000
#define HUGE_PAGE_SIZE	0x40000000

#define ROUND_UP(x,align)	((x+(align-1)) & ~(align-1))
#define ROUND_DOWN(x,align)	(x & ~(align-1))

#define	ALIGNED(x,align)	((x & (align-1))==0)

#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/arch_setup.h>

void arch_init_pmm(struct setup_info* arch_setup_info);

#endif
