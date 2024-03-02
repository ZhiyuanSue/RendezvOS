    .section .boot
    .global _start
    .align 8

_start:
	jmp _entry
_entry:
    /*set sp*/

    /*check magic*/

    /*save infomation*/
	call cmain
    jmp hlt
hlt:
    jmp hlt
