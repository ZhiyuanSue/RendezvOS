	.section .boot
	.global _start
	.align 4

.code32
_start:
	jmp multiboot_entry
	/* Align 32 bits boundary. */
	.align  4
multiboot_header:
	.long	0x1BADB002	/*magic*/
	.long	0x00010002	/*flags,only memory info and load address infos*/
	.long	-(0x1BADB002+0x00010002)   /*checksum*/
multiboot_header_address:	/*we use bin file, not elf file,so those are all need*/
	.long	multiboot_header
multiboot_load_address:
	.long	_start
multiboot_load_end_address:
	.long	_edata
multiboot_bss_end_address:
	.long	_end
multiboot_entry_address:
	.long	multiboot_entry
multiboot_header_end:
multiboot_entry:
	/*disable interrupt*/
	
	/*set sp*/
	movl 	$(boot_stack+0x10000),%esp
	/*clear the flag register*/
	pushl	$0
	popf
	/*store the multiboot info*/
	movl	%ebp,multiboot_info_struct
	/*check magic*/

	/*check CPUID whether it support IA-32e mode*/
cpuid_check:

calculate_kernel_pages:

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


	
/* some infomation under 32 bit*/	
	.section .boot.data
	.global multiboot_info_struct
multiboot_info_struct:
	.long 0
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO SHAMPOOS"
	
	.section .boot_page
	.align 0x1000
boot_page_table_PML4:
	.zero 8*512

.code64
x86_64_entry:
	call 	cmain
	jmp 	hlt
stack_field:
	.align 0x1000
	.comm boot_stack,0x10000
