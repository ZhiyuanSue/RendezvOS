ENTRY(_start)
kernel_virt_offset = 0xffffffff80000000 ;
SECTIONS {
    . = 0x100000;
	_kstart = .;
	_start = .;
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
