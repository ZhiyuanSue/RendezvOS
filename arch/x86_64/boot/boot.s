	.section .boot
	.global _start
	.align 4
/* some definations */
	.set	kernel_virt_offset,0xffffffff80000000
	.set	boot_stack_size,0x10000
	.set	multiboot_header_magic,0x1BADB002
	.set	multiboot_header_flag_addr,0x00000002
	.set	multiboot_header_flag_vbe,0x00010000
	.set	align_2m,0x200000
	.set	align_2m_minus_1,0x1fffff
	.set	align_bit32_not_2m_minus_1,0xffe00000
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
	movl	%eax,setup_info
	/*store the %ebx*/
	movl	%ebx,setup_info+4
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
	movl	%ebx,setup_info+8
	movb	%ah,%bl
	movl	%ebx,setup_info+12

calculate_kernel_pages:
	/*calculate the upper*/
	movl	$(_end-kernel_virt_offset),%eax	
	addl	$align_2m_minus_1,%eax
	andl	$align_bit32_not_2m_minus_1,%eax
	movl	%eax,%ebx
	
	/*calculate the lower*/
	movl	$(_kstart-kernel_virt_offset),%eax
	andl	$align_bit32_not_2m_minus_1,%eax
	
change_kernel_page_table:

load_kernel_page_table:


enable_pae:
	/*enable_pae*/

enable_lme:
	/*enable_lme*/


enable_pg:
	/*enable_pg*/

	call	cmain
	jmp 	hlt
hlt:
	call 	cpu_idle
ERROR_CPUID:
	jmp hlt

	
/* some infomation under 32 bit*/	
	.section .boot.data
	.global setup_info
setup_info:
	.long 0	/* multiboot magic */
	.long 0	/* multiboot info struct ptr */
	.long 0	/* phy addr width */
	.long 0	/* linear addr width*/
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO SHAMPOOS"
	
	.section .boot_page
	.align 0x1000
boot_page_table_PML4:
	.quad boot_page_table_directory_ptr - kernel_virt_offset
	.zero 8*510
	.quad boot_page_table_directory_ptr - kernel_virt_offset

	.global boot_page_table_directory_ptr
boot_page_table_directory_ptr:
	.zero 8*512

.code64
x86_64_entry:
	call 	cmain
	jmp 	hlt
stack_field:
	.align 0x1000
	.comm boot_stack,boot_stack_size
