#define	kernel_virt_offset	0xffff800000000000
#define	boot_stack_size	0x10000
#define	align_4k	0x1000
#define	align_2m	0x200000
#define	align_2m_minus_1	0x1fffff
#define	align_not_2m_minus_1	0xffffffffffe00000
#include <arch/aarch64/sys_ctrl_def.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <common/platform/aarch64_qemu_virt.h>
/* some definations */

	.section	.boot
	.global	_start
_start:
_boot_header:
	b _entry
	b _entry
	.quad	0x80000
	.quad	0	/*image size*/
	.quad	0	/*flags*/
	.quad	0
	.quad	0
	.quad	0
	.long	0x644d5241
	.long	0
_entry:
	/*save some info:x0(dtb),mpidr_el1*/
	adr	x4,setup_info
	stp	x0,x1,[x4]
	add	x4,x4,#16
	stp	x2,x3,[x4]
	/*try to drop to el1*/
	bl	drop_to_el1
	bl	sel_stack
	bl	set_stack
	/*prepare some page table*/
	bl	prepare_page_table
finish_prepare_page_table:
	/*init mmu and some registers*/
	bl	init_mmu
	/*enable fp*/
	bl	enable_fp
	/*clear bss segment*/
	bl	clear_bss
	/*set boot stack pointer*/
	bl	set_stack
	/*call cmain*/
	adr	x0,#setup_info
	bl	cmain
	b	boot_Error


	.global	drop_to_el1
drop_to_el1:
	/*switch to el1*/
	mrs	x0,CurrentEL
	cmp	x0,#CurrentEL_EL3
	b.ne	EL2_setup	
EL3_setup:
	/*TODO:set EL3*/
EL2_setup:
	cmp	x0,#CurrentEL_EL2
	b.ne	EL1_setup
	/*TODO:set SPSR*/

	/*TODO:set LR to ELR*/
	
	/*eret*/
	eret
EL1_setup:
	ret


	.global prepare_page_table
prepare_page_table:
	/*with the example of the qemu*/
	/*0000 0000 0000 0000 0000 0000 0000 0000 0100 0000 0000 1000 0000 0000 0000 0000*/
	/*0000 0000 0000 0000 unused*/
	/*                    0000 0000 0 index of the level0*/
	/*                               000 0000 01 index of the level1*/
	/*                                          00 0000 000 index of the level2*/
	/*1111 1111 1111 1111 1000 0000 0000 0000 0100 0000 0000 1000 0000 0000 0000 0000*/
	/*1111 1111 1111 1111 unused*/
	/*                    1000 0000 0 index of the level0*/
	/*other levels are the same*/
	/*calculate the address region*/
	/*x1 record the (align-1), x2 record the ~(align-1)*/
	mov	x1,#align_2m
	sub	x1,x1,#1
	mvn	x2,x1
	/*round down start and store in x6*/
	adr	x6,#_start
	and	x6,x6,x2
	/*round up end and store in x7*/
	adr	x7,#_end
	add	x7,x7,x1
	and x7,x7,x2

	/*we assume the start and end is in the same 1GB region,so check*/
	mov x0,x6
	lsr	x0,x0,#30
	mov x1,x7
	lsr	x1,x1,#30
	cmp	x0,x1
	b.ne	boot_Error
	/*======first identity mapping======*/
	/*update L0_table and L1_table*/
	/*======L0======*/

	adr	x0,#L1_table
	mov	x1,x6
	adr	x2,#L0_table
	mov	x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_BLOCK_OR_TABLE
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L0_entry

	/*======L1======*/

	adr	x0,#L2_table
	mov	x1,x6
	adr	x2,#L1_table
	mov	x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_BLOCK_OR_TABLE
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L1_entry

	/*======loop and add entries into the L2 block======*/
	mov	x4,x6
fill_in_entry_loop:
	cmp	x7,x4
	b.ls	#finish_fill_in_entry_loop

	mov	x0,x4
	mov x1,x4
	adr	x2,#L2_table
	mov	x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L2_entry

	add x4,x4,#align_2m
	b	fill_in_entry_loop

finish_fill_in_entry_loop:
	adr	x1,#setup_info
	mov	x2,#kernel_virt_offset
	add	x4,x4,x2
	str	x4,[x1,0x28]
	str	x4,[x1,0x30]
	/*======map some necessary range,like the pl011 uart,and now the x4 is the end of the kernel(round up)======*/
	/*map the 2m range*/

	adr	x0,#L3_table
	mov x1,x4
	adr	x2,#L2_table
	mov	x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_BLOCK_OR_TABLE
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L2_entry

	/*map the 4k range of the pl011*/

	mov	x0,#QEMU_VIRT_PL011_BASE_ADDR
	mov x1,x4
	adr	x2,#L3_table
	mov	x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_PAGE
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L3_entry

	adr	x1,#setup_info
	add	x4,x4,#align_4k
	str	x4,[x1,0x28]
	/*======second map to the real virtual addr=======*/
	/*as kernel_virt_offset is 0xffff800000000000, so we just need to update L0_table*/

	adr	x0,#L1_table
	mov	x1,x6
	mov	x2,#kernel_virt_offset
	add x1,x1,x2
	adr	x2,#L0_table
	mov x3,#PT_DESC_V
	orr	x3,x3,#PT_DESC_BLOCK_OR_TABLE
	orr	x3,x3,#PT_DESC_ATTR_LOWER_AF
	bl	arch_set_L0_entry
	
	b	finish_prepare_page_table

	.global init_mmu
init_mmu:
	/*fill in x0 and x1 the page table,fill in the x2 with the target tcr*/
	mrs	x2,TCR_EL1
	mrs x4,ID_AA64MMFR0_EL1
	and x4,x4,#ID_AA64MMFR0_EL1_PARANGE_MASK	/*support the usable phyaddr*/
	lsl	x4,x4,#32
	mov x5,#1
	lsl x5,x5,#33
	cmp x4,x5
	b.ls set_tcr_ips_end
	// limit the tcr el1 ips to 40bits
	mov x4,x5
set_tcr_ips_end:
	and	x4,x4,#TCR_EL1_IPS_MASK
	orr	x2,x2,x4
	/*set ttbr0*/
	/*orr x2,x2,#TCR_EL1_EPD0_ENABLE_WALK*/ /*for it's 0*/
	/*orr	x2,x2,#TCR_EL1_TG0_4KB*/	/*for it's 0*/
	orr x2,x2,#TCR_EL1_SH0_INNER_SHARED_CACHE
	orr x2,x2,#TCR_EL1_ORGN0_WB_RA_WA_CACHE
	orr x2,x2,#TCR_EL1_IRGN0_WB_RA_WA_CACHE
	mov x4,#0x10
	orr x2,x2,x4

	/*set ttbr1*/
	/*orr x2,x2,#TCR_EL1_EPD1_ENABLE_WALK*/	/*for it's 0*/
	orr	x2,x2,#TCR_EL1_TG1_4KB
	orr x2,x2,#TCR_EL1_SH1_INNER_SHARED_CACHE
	orr x2,x2,#TCR_EL1_ORGN1_WB_RA_WA_CACHE
	orr x2,x2,#TCR_EL1_IRGN1_WB_RA_WA_CACHE
	mov	x4,#(0x10<<16)
	orr	x2,x2,x4
	
	/*fill in ttbr0 and 1,and fill in the tcr_el1*/
	adr	x0,#L0_table
	adr x1,#L0_table
	msr	TTBR0_EL1,x0
	msr	TTBR1_EL1,x1
	msr	TCR_EL1,x2
	isb
	mrs	x0,SCTLR_EL1
	mov x0,#SCTLR_EL1_M
	orr x0,x0,#SCTLR_EL1_C
	orr	x0,x0,#SCTLR_EL1_I
	msr	SCTLR_EL1,x0
	isb

	adr	x0,#jmp_to_upper_addr
	mov	x1,#kernel_virt_offset
	add	x0,x0,x1
	add	lr,lr,x1
	br	x0
jmp_to_upper_addr:	
	ret

	.global	enable_fp
enable_fp:
	mrs	x0,CPACR_EL1
	orr	x0,x0,#CPACR_EL1_FPEN_NONE
	msr	CPACR_EL1,x0
	ret

	.global	clear_bss
clear_bss:
	adr	x0,#_bss_start
	adr	x1,#_bss_end
	mov	x2,#0
	mov x3,x2
	mov	x4,#0x10
clear_bss_loop:
	cmp	x1,x0
	b.ls	finish_clear_bss_loop_1
	stp	x2,x3,[x0]
	add	x0,x0,x4
	b	clear_bss_loop

finish_clear_bss_loop_1:
	cmp	x0,x1
	b.eq	finish_clear_bss_loop_2
	str	x2,[x0]
finish_clear_bss_loop_2:
	ret

	.global	sel_stack
sel_stack:
	mrs	x0,SPSel
	orr	x0,x0,#SPSEL_SP_ELx
	msr	SPSel,x0
	ret
	
	.global	set_stack
set_stack:
	adr	x0,#boot_stack
	add	x0,x0,#boot_stack_size
	mov	sp,x0
	ret

/*======data======*/	

	.section .boot.data
	.p2align	4	/*2^4=16*/
	.global setup_info
setup_info:
	.quad	0	/*x0,for dtb*/
	.quad	0	/*x1,reserved*/
	.quad	0	/*x2,reserved*/
	.quad	0	/*x3,reserved*/
	.quad	log_buffer
	.quad	0	/*the map address end*/

	.section .boot.page
	.p2align	12	/*2^12=4K*/
	/*we just use 48bit page and the 4K*/
	.global	L0_table
L0_table:
	.zero 8*512
	.global	L1_table
L1_table:
	.zero 8*512
	.global	L2_table
L2_table:
	.zero 8*512
	.global	L3_table
L3_table:
	.zero 8*512

boot_Error:
	b	boot_Error

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
stack_field:
	.p2align 12	/*2^12=4096*/
	.comm boot_stack,boot_stack_size