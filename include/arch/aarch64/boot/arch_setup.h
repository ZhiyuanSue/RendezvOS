#ifndef _SHAMPOOS_ARCH_SETUP_H_
#define _SHAMPOOS_ARCH_SETUP_H_
#include <common/types.h>
#define KERNEL_VIRT_OFFSET 0xffffffffc0000000

struct boot_header {
	u32 code0;
	u32 code1;
	u64 text_offset;
	u64 image_size;
	u64 flags;
	u64 res2;
	u64 res3;
	u64 res4;
	u32 magic;
	u32 res5;
} __attribute__((packed));

struct setup_info {
	u64 dtb_ptr;				   /*0x0*/
	u64 res_x1;					   /*0x8*/
	u64 res_x2;					   /*0x10*/
	u64 res_x3;					   /*0x18*/
	u64 log_buffer_addr;		   /*0x20*/
	u64 map_end_virt_addr;		   /*0x28*/
	u64 boot_uart_base_addr;	   /*0x30*/
	u64 boot_dtb_header_base_addr; /*0x38*/
} __attribute__((packed));

int start_arch(struct setup_info *arch_setup_info);
#endif