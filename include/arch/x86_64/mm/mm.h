#ifndef _SHAMPOOS_MM_H_
#define _SHAMPOOS_MM_H_
#ifndef _SHAMPOOS_KERNEL_OFFSET_
#define _SHAMPOOS_KERNEL_OFFSET_ 0xffffffffc0000000
#define KERNEL_PHY_TO_VIRT(phy_addr) (phy_addr+_SHAMPOOS_KERNEL_OFFSET_)
#define KERNEL_VIRT_TO_PHY(virt_addr) (virt_addr-_SHAMPOOS_KERNEL_OFFSET_)
#endif
#endif