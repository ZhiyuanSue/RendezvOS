#ifndef _SHAMPOOS_ARCH_SETUP_H_
#define _SHAMPOOS_ARCH_SETUP_H_
#include <common/types.h>
#define KERNEL_VIRT_OFFSET 0xffffffffc0000000
struct setup_info{
	u64	dtb_ptr;
	u64 log_buffer_addr;
} __attribute__((packed));

int	start_arch (struct setup_info* arch_setup_info);
#endif