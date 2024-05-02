ENTRY(_start)

SECTIONS {

	_kstart = .;
	_start = .;
	.text :
	{
		*(.boot)
		*(.text .text.*)
	}
	. = ALIGN(4K)
	_sdata = .;
	.data :
	{
		*(.data .data.*)
	}
	_edata = .;
	. = ALIGN(4K);
	_bss_start = .;
	.bss :
	{
		*(.bss .bss.*)
	}
	_bss_end = .;
	_end = .;
}