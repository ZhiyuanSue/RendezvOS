#include "multiboot2_tag.h"
    .global .boot
    .align 8
_start:
	jmp multiboot_entry
    .align 4
multiboot_header:
    .long   0xE85250D6  /*magic*/
    .long   0x0         /*archtecture,0 means 32-bit i386*/
    .long   multiboot_header_end-multiboot_header        /*header length*/
    .long   -(0xE85250D6+0x0+multiboot_header_end-multiboot_header)   /*checksum*/

multiboot_header_end:
multiboot_entry:
    /*set sp*/

    /*check magic*/

    /*save infomation*/
	call cmain
    jmp hlt
hlt:
    jmp hlt
