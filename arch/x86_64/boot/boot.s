/* some definations */
	.set	kernel_virt_offset,0xffffffffc0000000
	.set	boot_stack_size,0x10000
	.set	multiboot_header_magic,0x1BADB002
	.set	multiboot_header_flag_addr,0x00000002
	.set	multiboot_header_flag_vbe,0x00010000
	.set	align_2m,0x200000
	.set	align_2m_minus_1,0x1fffff
	.set	align_bit32_not_2m_minus_1,0xffe00000
	.set	CR0_PG,0x80000000
	.set	CR4_PGE_OR_PAE,0xa0
	.set	IA32_EFER_bits,0x901
	.set	IA32_EFER_addr,0xC0000080

/*code under 32bit*/
	.section .boot
	.global _start
	.align 4
.code32
_start:
	jmp multiboot_entry
	/* Align 32 bits boundary. */
	.align  4
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
	/*disable interrupt*/
	
	/*store the multiboot magic*/
	movl	%eax,(setup_info - kernel_virt_offset)
	/*store the %ebx*/
	movl	%ebx,(setup_info + 4 - kernel_virt_offset)
	/*set sp*/
	movl 	$(boot_stack + boot_stack_size - kernel_virt_offset),%esp
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
	movl	$(boot_page_table_directory-kernel_virt_offset),%edx
change_kernel_page_table_loop:
	movl	%eax,%ecx	/*store the eax to ecx*/
	andl	$align_bit32_not_2m_minus_1,%eax	/*for the phy addr must not below 1GB,so and this mask is possible*/
	orl		$0x183,%eax
	movl	%eax,(%edx)
	addl	$8,%edx	/*add a quad position*/
change_kernel_page_table:
	movl	%ecx,%eax
	addl	$align_2m,%eax
	cmp	%eax,%ebx
	jge	change_kernel_page_table_loop
load_kernel_page_table:
	movl	$(boot_page_table_PML4-kernel_virt_offset),%eax
	movl	%eax,%cr3

enable_pae:
	/*enable_pae*/
	movl	%cr4,%eax
	orl		$CR4_PGE_OR_PAE,%eax
	movl	%eax,%cr4
enable_lme:
	/*enable_lme*/
	movl	$IA32_EFER_addr,%ecx
	rdmsr
	orl	$IA32_EFER_bits,%eax /*IA32_EFER.SCE[bit0] and IA32_EFER.LMR[bit8] and IA32_EFER.NXE[bit11] should be set*/
	wrmsr

enable_pg:
	/*enable_pg*/
	movl	%cr0,%eax
	orl		$CR0_PG,%eax
	movl	%eax,%cr0
long_jmp_to_64:
	lgdtl	tmp_gdt_desc-kernel_virt_offset
	ljmp	$0x08,$(x86_64_entry-kernel_virt_offset)
	jmp 	hlt
hlt:
	call 	cpu_idle
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
	movq	$(boot_stack+boot_stack_size),%rsp
clean_tmp_page_table:
	movq	%rax,(boot_page_table_PML4)
	movq	%rax,(boot_page_table_directory_ptr)
run_main:
	mov	$(setup_info),%rdi
	call	cmain
	jmp	hlt


/* some infomation under 32 bit*/	
	.section .boot.data
	.align 4
	.global setup_info
setup_info:
	.long 0	/* multiboot magic */
	.long 0	/* multiboot info struct ptr */
	.long 0	/* phy addr width */
	.long 0	/* linear addr width*/
	.quad log_buffer /*log buffer position*/
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO SHAMPOOS"
	
	.align 8
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
	.align 0x1000
boot_page_table_PML4:
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
	.quad	boot_page_table_directory_ptr - kernel_virt_offset + 0x3
	.zero	8*510
	.quad	boot_page_table_directory_ptr - kernel_virt_offset + 0x7
	
	.global boot_page_table_directory_ptr
boot_page_table_directory_ptr:
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
	.quad	boot_page_table_directory - kernel_virt_offset + 0x3
	.zero	8*510
	.quad	boot_page_table_directory - kernel_virt_offset + 0x3

	.global boot_page_table_directory
boot_page_table_directory:
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
		the start pos is 1111 1111 1111 1111 1111 1111 1111 1111 1100 0000 0000 0000 0000 0000 0000 0000 b
		and the first  | 1...              1 | is pre 1 bits 
		and the                             |1111 1111 1| is the PML4 addr, which will find the last entry of PDPT
		and then                                       |111 1111 11| is the PDPT addr, which will find the last entry of PDE
		and find the boot_page_table_directory,and set this entry's PS bit 
		and we don't think the kernel might need more then 1GB space on shampoos
	*/
	.section .boot.log
	.align 0x1000
	.global log_buffer
log_buffer:
	.zero 0x10000
	/*stack*/
stack_field:
	.align 0x1000
	.comm boot_stack,boot_stack_size
