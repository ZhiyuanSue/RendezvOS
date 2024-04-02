#ifndef _ARCH_SETUP_H_
#define _ARCH_SETUP_H_
#include <shampoos/types.h>

#define KERNEL_VIRT_OFFSET 0xffffffffc0000000

struct setup_info{
	u32	multiboot_magic;
	u32	multiboot_info_struct_ptr;
	u32	phy_addr_width;
	u32	vir_addr_width;
	u64 log_buffer_addr;
} __attribute__((packed));

#endif