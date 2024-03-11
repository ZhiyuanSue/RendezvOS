ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
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
		*(.boot_page)
		*(.boot_stack)
		*(.bss)
	}
	_end = .;
}
