ENTRY(_start)
OUTPUT_ARCH("aarch64")
OUTPUT_FORMAT("elf64-littleaarch64")
SECTIONS {
	kernel_virt_offset = 0xffff800000000000 ;
	kernel_start_offset = 0x40080000 ;
	. = kernel_virt_offset + kernel_start_offset;
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
		*(.boot.data)
		. = ALIGN(4K);
		*(.boot.page)
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