ENTRY(_start)
SECTIONS {
    . = 0x100000;
	_kstart = .;
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
	.bss :
	{
		*(.boot_stack)
		*(.bss)
	}
    _end = .;
}
