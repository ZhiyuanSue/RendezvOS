#define	kernel_virt_offset	0xffff800000000000
#define	boot_stack_size	0x10000
#include <arch/aarch64/sys_ctrl_def.h>
/* some definations */

	.section	.boot
	.global	_start
_start:
_boot_header:
	b bsp_entry
	b bsp_entry
	.quad	0x80000
	.quad	0	/*image size*/
	.quad	0	/*flags*/
	.quad	0
	.quad	0
	.quad	0
	.long	0x644d5241
	.long	0
bsp_entry:
	/*save some info:x0(dtb),mpidr_el1*/
	adr	x4,setup_info
	stp	x0,x1,[x4]
	add	x4,x4,#16
	stp	x2,x3,[x4]
	/*try to drop to el1*/
	bl	drop_to_el1
	bl	sel_stack
	bl	set_stack
	/*set mair for paging*/
	bl  mair_init
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
	adr	x0,#_start
	adrp	x1,_end
	add x1,x1, :lo12:_end
	adr	x2,#L0_table
	adr	x3,#L1_table
	adr	x4,#L2_table
	adr	x5,#L3_table
	adr	x6,#setup_info
	bl	boot_map_pg_table
	b finish_prepare_page_table

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
	orr x2,x2,#TCR_EL1_HA
	orr x2,x2,#TCR_EL1_HD
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
	adrp	x0,_bss_start
	add	x0,x0, :lo12:_bss_start
	adrp	x1,_bss_end
	add	x1,x1, :lo12:_bss_end
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

/*======ap_start======*/
	.global ap_entry
ap_entry:
	adr	x4,setup_info
	add x4,x4,#0x48
	str x0,[x4]
	bl drop_to_el1
	bl sel_stack
	bl ap_set_stack_phy
	bl  mair_init
	bl	init_mmu
	bl	enable_fp
	bl	ap_set_stack_virt
	adr	x0,#setup_info
	bl	start_secondary_cpu
	b boot_Error

	.global ap_set_stack
ap_set_stack_phy:
	adr	x4,setup_info
	add x4,x4,#0x40
	ldr x0,[x4]
	mov x4,#kernel_virt_offset
	sub x0,x0,x4
	mov sp,x0
	ret

	.global ap_set_stack
ap_set_stack_virt:
	adr	x4,setup_info
	add x4,x4,#0x40
	ldr x0,[x4]
	mov sp,x0
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
	.quad	0	/*ap_boot_stack_ptr*/
	.quad	0	/*cpu id*/

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

	.global boot_Error
boot_Error:
	b	boot_Error

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
stack_field:
	.p2align 12	/*2^12=4096*/
	.global boot_stack
boot_stack:
	.zero 0x10000