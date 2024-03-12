ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
SECTIONS {
	kernel_virt_offset = 0xffffffff80000000 ;
	kernel_start_offset = 0x100000 ;
	. = kernel_virt_offset + kernel_start_offset;
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
		*(.boot_page)
		*(.boot_stack)
		*(.bss)
	}
	_end = .;
}
