ENTRY(_start)
OUTPUT_ARCH("aarch64")
OUTPUT_FORMAT("elf64-littleaarch64")
SECTIONS {

	_kstart = .;
	_start = .;
	.text :
	{
		*(.boot)
		*(.text .text.*)
	}
	. = ALIGN(4K);
	_sdata = .;
	.data :
	{
		*(.boot.log)
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