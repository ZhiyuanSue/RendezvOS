#define	kernel_virt_offset	0xffff800000000000
#define	boot_stack_size	0x10000
#define	CurrentEL_EL0	0x0
#define	CurrentEL_EL1	0x4
#define	CurrentEL_EL2	0x8
#define	CurrentEL_EL3	0xC
#define	SCTLR_EL1_M	0x1
#define	SCTLR_EL1_I	(1<<12)
#define	SCTLR_EL1_C	(1<<2)
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

	/*fill in ttbr0 and 1,and fill in the tcr_el1*/
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
	.align	4	/*2^4=16*/
	.global setup_info
setup_info:
	.quad	0	/*x0,for dtb*/
	.quad	0	/*x1,reserved*/
	.quad	0	/*x2,reserved*/
	.quad	0	/*x3,reserved*/
	.quad	log_buffer

	.section .boot.page
	/*we just use 48bit page and the 4K*/

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
stack_field:
	.align 12	/*2^12=4096*/
	.zero boot_stack_size