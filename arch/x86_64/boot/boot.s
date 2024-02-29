
    .section .boot
_start:
	jmp multiboot_entry
multiboot_header:
    

multiboot_entry:
	call cmain
    