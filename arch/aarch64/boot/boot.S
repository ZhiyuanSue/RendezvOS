#define	kernel_virt_offset	0xffff800000000000
#define	boot_stack_size	0x10000

#include <arch/aarch64/sys_ctrl_def.h>

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
	/*prepare some page table*/
	bl	prepare_page_table
	/*init mmu and some registers*/
	bl	init_mmu
	/*clear bss segment*/

	/*set boot stack pointer*/

	/*call cmain*/
	b _start


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
	
	ret

	.global init_mmu
init_mmu:
	/*fill in x0 and x1 the page table,fill in the x2 with the target tcr*/
	mrs	x2,TCR_EL1
	mrs x4,ID_AA64MMFR0_EL1
	and x4,x4,#ID_AA64MMFR0_EL1_PARANGE_MASK	/*support the usable phyaddr*/
	lsl	x4,x4,#32
	and	x4,x4,#TCR_EL1_IPS_MASK
	//mov x4,#TCR_EL1_IPS_48bit
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
	ret

	.section .boot.data
	.p2align	4	/*2^4=16*/
	.global setup_info
setup_info:
	.quad	0	/*x0,for dtb*/
	.quad	0	/*x1,reserved*/
	.quad	0	/*x2,reserved*/
	.quad	0	/*x3,reserved*/
	.quad	log_buffer

	.section .boot.page
	.p2align	12	/*2^12=4K*/
	/*we just use 48bit page and the 4K*/
L0_table:
	.zero 8*512
L1_table:
	.zero 8*512
L2_table:
	.zero 8*512

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
stack_field:
	.p2align 12	/*2^12=4096*/
	.zero boot_stack_size