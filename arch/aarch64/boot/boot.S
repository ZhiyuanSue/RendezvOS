#define	kernel_virt_offset	0xffff800000000000
#define	CurrentEL_EL0	0x0
#define	CurrentEL_EL1	0x4
#define	CurrentEL_EL2	0x8
#define	CurrentEL_EL3	0xC
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
	
EL2_setup:
	cmp	x0,#CurrentEL_EL1
	b.ne	EL1_setup
	/*set SPSR*/

	/*set LR to ELR*/
	mov	ELR,X30
	/*eret*/
	eret
EL1_setup:
	ret

	.global prepare_page_table
prepare_page_table:

	ret

	.global init_mmu
init_mmu:
	ret

	.section .boot.data
	.align	4	/*2^4=16*/
	.global setup_info
setup_info:
	.quad	0
	.quad	log_buffer

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
