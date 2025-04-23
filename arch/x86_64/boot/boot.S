#define	kernel_virt_offset 0xffff800000000000
#define	boot_stack_size	0x10000
#define	multiboot_header_magic	0x1BADB002
#define	multiboot_header_flag_addr	0x00000002
#define	multiboot_header_flag_vbe	0x00010000
#define	align_2m	0x200000
#define	align_2m_minus_1	0x1fffff
#define	align_bit32_not_2m_minus_1	0xffe00000
#define ap_phy_addr 0x1000
#include	<arch/x86_64/sys_ctrl_def.h>
#include	<arch/x86_64/mm/page_table_def.h>
#include	<arch/x86_64/msr.h>
/* some definations */

/* some macro */
.macro load_kernel_page_table
	movl	$(L0_table-kernel_virt_offset),%eax
	movl	%eax,%cr3
.endm

.macro enable_pae
	/*enable_pae*/
	movl	%cr4,%eax
	orl		$(CR4_PGE | CR4_PAE),%eax
	movl	%eax,%cr4
.endm

.macro enable_lme
	/*enable_lme*/
	movl	$IA32_EFER_addr,%ecx
	rdmsr
	orl	$(IA32_EFER_SCE | IA32_EFER_LMR | IA32_EFER_NXE),%eax
	/*IA32_EFER.SCE[bit0] and IA32_EFER.LMR[bit8] and IA32_EFER.NXE[bit11] should be set*/
	wrmsr
.endm

.macro enable_pg
	/*enable_pg*/
	movl	%cr0,%eax
	orl		$CR0_PG,%eax
	movl	%eax,%cr0
.endm

/*code under 32bit*/
	.section .boot
	.global _start
	.p2align 2
.code32
_start:
	jmp multiboot_entry
	/* Align 32 bits boundary. */
	.p2align  2
multiboot_header:
	.long	multiboot_header_magic	/*magic*/
	.long	multiboot_header_flag_addr | multiboot_header_flag_vbe	/*flags,only memory info and load address infos*/
	.long	-(multiboot_header_magic + multiboot_header_flag_addr | multiboot_header_flag_vbe )   /*checksum*/
multiboot_header_address:	/*we use bin file, not elf file,so those are all need*/
	.long	multiboot_header - kernel_virt_offset
multiboot_load_address:
	.long	_start - kernel_virt_offset
multiboot_load_end_address:
	.long	_edata - kernel_virt_offset
multiboot_bss_end_address:
	.long	_end - kernel_virt_offset
multiboot_entry_address:
	.long	multiboot_entry - kernel_virt_offset
multiboot_header_end:
multiboot_entry:
bsp_entry:
	/*disable interrupt*/
	
	/*store the multiboot magic*/
	movl	%eax,(setup_info - kernel_virt_offset)
	/*store the %ebx*/
	movl	%ebx,(setup_info + 4 - kernel_virt_offset)
	/*as we have set the stack at bss section, we must clear it first*/
clear_bss_start:
	movl	$(_bss_start-kernel_virt_offset),%eax
	movl	$(_bss_end-kernel_virt_offset),%ebx
clear_bss:
	cmp		%eax,%ebx
	jg		set_sp
	movl	$0,(%eax)
	add		$1,%eax
	jmp		clear_bss
set_sp:
	/*set sp*/
	movl 	$(_boot_stack_top - kernel_virt_offset),%esp
	/*clear the flag register*/
	pushl	$0
	popf
	/*check magic*/

	/*check CPUID whether it support IA-32e mode*/
cpuid_check:
	movl	$0x80000001,%eax	/*check the CPUID */
	cpuid	/*EDX.LM[bit29] must be support*/
	andl	$(1<<29),%edx
	cmp	$0,%edx
	je	ERROR_CPUID

	movl	$0x80000008,%eax	/*check the CPUID*/
	cpuid	/*EAX[7:0] (I think is al) means phy address width, and EAX[15:8] means linear address width*/
	movl	$0,%ebx
	movb	%al,%bl
	movl	%ebx,(setup_info + 8 - kernel_virt_offset)
	movb	%ah,%bl
	movl	%ebx,(setup_info + 12 - kernel_virt_offset)

calculate_kernel_pages:
	/*calculate the upper*/
	movl	$(_end-kernel_virt_offset),%eax	
	addl	$align_2m_minus_1,%eax
	andl	$align_bit32_not_2m_minus_1,%eax
	movl	%eax,%ebx
	
	/*calculate the lower*/
	movl	$(_kstart-kernel_virt_offset),%eax
	andl	$align_bit32_not_2m_minus_1,%eax
	movl	$(L2_table-kernel_virt_offset),%edx
change_kernel_page_table_loop:
	movl	%eax,%ecx	/*store the eax to ecx*/
	andl	$align_bit32_not_2m_minus_1,%eax	/*for the phy addr must not below 1GB,so and this mask is possible*/
	orl		$( PDE_P | PDE_RW | PDE_PS | PDE_G),%eax
	movl	%eax,(%edx)
	addl	$8,%edx	/*add a quad position*/
change_kernel_page_table:
	movl	%ecx,%eax
	addl	$align_2m,%eax
	cmp	%eax,%ebx
	jg	change_kernel_page_table_loop

	enable_pae
	enable_lme
	load_kernel_page_table
	enable_pg

long_jmp_to_64:
	lgdtl	tmp_gdt_desc-kernel_virt_offset
	ljmp	$0x08,$(x86_64_entry-kernel_virt_offset)
	jmp 	hlt
hlt:
	jmp		hlt
ERROR_CPUID:
	jmp hlt

	
/*code under 64bit*/
	.section .boot_64
.code64
x86_64_entry:
	movabs	$(set_segments),%rax
	jmp	*%rax
set_segments:
	xor	%rax,%rax
	movw	%ax,%ds
	movw	%ax,%ss
	movw	%ax,%fs
	movw	%ax,%gs
set_new_stack:
	movabs	$(_boot_stack_top),%rsp
run_main:
	movabs	$(setup_info),%rdi
	call	cmain
	hlt
	jmp	hlt

	.global clean_tmp_page_table
clean_tmp_page_table:
	movabs	%rax,(L0_table)
	ret

	.section .boot.ap_code
	.p2align 12
	.global ap_start
.code16
ap_start:
	wbinvd

	xor %ax,%ax
	mov %ax,%ds
	mov %ax,%ss
	mov %ax,%fs
	mov %ax,%gs

	lgdtl	ap_tmp_gdt_desc - ap_start + ap_phy_addr
	movl	%cr0,%eax
	orl		$CR0_PE,%eax
	movl	%eax,%cr0
	ljmp	$0x08,$(ap_start_32 - ap_start + ap_phy_addr)

	.p2align 4
.code32
ap_start_32:
	enable_pae
	enable_lme
	load_kernel_page_table
	enable_pg

ap_long_jmp_to_64:
	lgdtl	tmp_gdt_desc - kernel_virt_offset
	ljmp	$0x08,$(ap_x86_64_entry - kernel_virt_offset)
	jmp 	hlt

ap_tmp_gdt:
	.quad 0x0	# 0x00: null
	.quad ((1<<(32+12))	| (1<<(32+23)) | (1<<(32+22)) | (1<<(32+15)) | (1<<(32+11)) | (1<<(32+10)) | (1<<(32+9)) | (0xf<<(32+16) | (0xffff))) 
	/*
		bit 44 means a code or data segment,and if it's a system segment,must be cleared
		bit 54 means a 32bit mode
		bit 55 means the granularity
		bit 43 means a code segment
		bit 42 means a coherant segment
		bit 41 means R/W
		bit 47 means P(present)
		DPL=0
	*/
ap_tmp_gdt_desc:
	.short	( . - ap_tmp_gdt - 1)
	.quad	ap_tmp_gdt - ap_start + ap_phy_addr

    .p2align 3
.code64
ap_x86_64_entry:
	movabs	$(ap_set_segments),%rax
	jmp	*%rax
ap_set_segments:
	xor	%rax,%rax
	movw	%ax,%ds
	movw	%ax,%ss
	movw	%ax,%fs
	movw	%ax,%gs
ap_set_new_stack:
	movabs	$(ap_boot_stack_ptr),%rax;
	mov		(%rax),%rsp
ap_run_main:
	movabs	$(setup_info),%rdi
	call	start_secondary_cpu
	jmp hlt

	.global ap_start_end
ap_start_end:
	hlt



/* some infomation under 32 bit*/	
	.section .boot.data
	.p2align 3
	.global setup_info
setup_info:
	.long 0	/* multiboot magic */
	.long 0	/* multiboot info struct ptr */
	.long 0	/* phy addr width */
	.long 0	/* linear addr width*/
	.quad log_buffer /*log buffer position*/
	.quad 0	/*rsdp ptr*/
ap_boot_stack_ptr:
    .quad 0 /*ap_boot_stack_ptr*/;
	.quad 0 /*cpu_id*/
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO RENDEZVOS"
	
	.p2align 3
tmp_gdt:
	.quad 0x0	# 0x00: null
	.quad ((1<<(32+12)) | (1<<(32+21)) | (1<<(32+15)) | (1<<(32+11)) | (1<<(32+10)) | (1<<(32+9)))	
	/*
		bit 44 means a code or data segment,and if it's a system segment,must be cleared
		bit 53 means a long mode
		bit 43 means a code segment
		bit 42 means a coherant segment
		bit 41 means R/W
		bit 47 means P(present)
		DPL=0
	*/
tmp_gdt_desc:
	.short	( . - tmp_gdt - 1)
	.quad	tmp_gdt - kernel_virt_offset



/**/
	.section .boot.page
	.p2align 12
	.global L0_table
L0_table:
	/*
		PML4_entry reference a pdpt 
		bit 0:present,must be 1
		bit 1:R/W,must be 1
		bit 2:U/S,must be 0
		bit 3:PWT,0
		bit 4:PCD,0
		bit 5:A,0
		bit 6:ignore
		bit 7:must 0
		bit 11 - 8:ignore
		bit M-1 - 12:4k align addr 
		bit 51 - M:must be 0
		bit 62:52:ignore
		bit 63:XD,disable-execute
	*/
	.quad	L1_table - kernel_virt_offset + ( PML4E_P | PML4E_RW )
	.zero	8*255
	.quad	L1_table - kernel_virt_offset + ( PML4E_P | PML4E_RW | PML4E_US)
	.zero	8*255
	
	.global L1_table
L1_table:
	/*
		PDPT_entry
		bit 0:present,must be 1
		bit 1:R/W,must be 1
		bit 2:U/S,must be 0
		bit 3:PWT,0
		bit 4:PCD,0
		bit 5:A,0
		bit 6:ignore
		bit 7:must 0
		bit 11 - 8:ignore
		bit M-1 - 12:4k align addr 
		bit 51 - M:must be 0
		bit 62:52:ignore
		bit 63:XD,disable-execute
	*/
	/*
		please remember one thing, current addr is 0x100000+offset
		so the first entry of pdpt table must also set to the pd
	*/
	.quad	L2_table - kernel_virt_offset + (PDPTE_P | PDPTE_RW)
	.zero	8*511

	.global L2_table
L2_table:
	/*
		PD_entry
		bit 0:present,must be 1
		bit 1:R/W,must be 1
		bit 2:U/S,must be 0
		bit 3:PWT,0
		bit 4:PCD,0
		bit 5:A,0
		bit 6:Dirty
		bit 7:must be 1
		bit 8:global,set to 1 to enable the global page cache
		bit 11 - 9:ignore
		bit 12:PAT,memory type 
		bit 20 - 13:ignore
		bit M-1 - 21:4k align addr 
		bit 51 - M:must be 0
		bit 58 - 52:ignored
		bit 62 - 59:protection key
		bit 63:XD,disable-execute
	*/
	.zero 8*512
	/*
		the start pos is 1111 1111 1111 1111 1111 1111 1000 0000 0000 0000 0000 0000 0000 0000 0000 0000 b
		and the first  | 1...              1 | is pre 1 bits 
		and the                             |1111 1111 1| is the PML4 addr, which will find the last entry of PDPT
		and then                                       |000 0000 00| is the PDPT addr, which will find the first entry of PDE
		and find the L2_table,and set this entry's PS bit 
	*/
	.section .boot.log
	.p2align 12
	.global log_buffer
log_buffer:
	.zero 0x10000
	/*stack*/
stack_field:
	.p2align 12
	.comm boot_stack,boot_stack_size
