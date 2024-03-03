ENTRY(_entry)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
SECTIONS {
	. = 0x100000;
	_entry= .
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
