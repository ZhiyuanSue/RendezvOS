
    .section .boot
_start:
	jmp multiboot_entry
    .align 4
multiboot_header:
    .long   0xE85250D6  /*magic*/
    .long   0x0         /*archtecture,0 means 32-bit i386*/
    .long   multiboot_entry-multiboot_header        /*header length*/
    .long   -(0xE85250D6+0x0+multiboot_entry-multiboot_header)   /*checksum*/
multiboot_entry:
	call cmain
    jmp hlt
hlt:
    jmp hlt