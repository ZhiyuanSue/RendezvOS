	.section .boot
	.global _start
	.align 4

.code32
_start:
	jmp multiboot_entry
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
	cmp		%eax,0x2BADB002
	jnz 	hlt
	jmp		x86_32_entry
	jmp 	hlt
hlt:
	jmp 	hlt

x86_32_entry:
	call 	cmain
	
/* some infomation under 32 bit*/	
	.section .data
	.global multiboot_info_struct
multiboot_info_struct:
	.long 0
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO SHAMPOOS"



.code64
x86_64_entry:

stack_field:
	.align 0x10000
	.comm boot_stack,0x10000
