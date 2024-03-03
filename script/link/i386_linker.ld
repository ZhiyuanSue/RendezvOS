ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
SECTIONS {
    . = 0x100000;
    .text : 
    {
        *(.boot)
        *(.text)
    }
    . =ALIGN(4K);
    .data :
    {
        *(.data)
    }
    _edata = .;
    _end = .;
}
