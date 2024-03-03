	.section .boot
	.global _entry
	.align 4
.code 32
_entry:
	jmp multiboot_entry
multiboot_header:
	.long	0x1BADB002	/*magic*/
	.long	0x00010002	/*flags,only memory info and load address infos*/
	.long	-(0x1BADB002+0x00010002)   /*checksum*/
multiboot_header_address:	/*we use bin file, not elf file,so those are all need*/
	.long	multiboot_header
multiboot_load_address:
	.long	_entry
multiboot_load_end_address:
	.long	_edata
multiboot_bss_end_address:
	.long	_end
multiboot_entry_address:
	.long	_entry
multiboot_header_end:
multiboot_entry:
	/*set sp*/
	mov sp
	/*push into the function call stack*/

	/*check magic*/

	/*save infomation and call main*/
	call start_arch
	jmp hlt
hlt:
	jmp hlt

.code 64

