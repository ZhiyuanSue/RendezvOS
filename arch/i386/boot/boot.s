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
multiboot_entry_display:
	.long 0
    .long 1024
    .long 768
    .long 32
multiboot_header_end:
multiboot_entry:
	/*set sp*/
	movl 	$(boot_stack+0x10000),%esp
	/*clear the flag register*/
	pushl	$0
    popf
	/*store the multiboot info*/
	movl	%ebp,multiboot_info_struct
	
	/*push into the function call stack*/

	/*check magic*/

	/*save infomation and call main*/
	call	cmain
	jmp		hlt
hlt:
	call cpu_idle
.section .data
	.global multiboot_info_struct
multiboot_info_struct:
	.long 0
multiboot_error_magic:
	.asciz "ERROR MAGIC NUM"
multiboot_hello:
	.asciz "HELLO SHAMPOOS"
Halted_str:
	.asciz  "Halted."
stack_field:
	.comm boot_stack,0x10000
