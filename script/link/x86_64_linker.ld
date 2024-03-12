ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
kernel_virt_offset = 0xffffffff80000000 ;
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
