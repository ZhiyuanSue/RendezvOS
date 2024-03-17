#ifndef _ARCH_SETUP_H_
#define _ARCH_SETUP_H_
#include <shampoos/types.h>

struct setup_info{
	u32	multiboot_magic;
	u32	multiboot_info_struct_ptr;
	u32	phy_addr_width;
	u32	vir_addr_width;
};

#endif