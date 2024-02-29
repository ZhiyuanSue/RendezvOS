
    .section .boot
_start:
	jmp multiboot_entry
	
multiboot_entry:
	call cmain
    