OUTPUT_ARCH("riscv")
OUTPUT_FORMAT("elf64-littleriscv")
ENTRY(_start)
SECTIONS{
    . = 0x80200000;
	_kstart = .;
    .text : {
        *(.boot)
        *(.text)
    }
    . = ALIGN(4K);
    .data : {
        *(.data)
    }
    . = ALIGN(4K);
    .rodata : {
        *(.rodata)
    }
    . = ALIGN(4K);
    .bss : {
        *(.boot.stack)
        *(.bss)
    }
    _bss_end = .;
    . += 4K*8;
    _runtime_stack_top = . ;
    _console_lower = .;
    . += 4K;
    _console_upper = .;
}