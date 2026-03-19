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
		*(.boot.*)
	}
	. = ALIGN(8);
	_s_init = .;
	.init :
	{
		*(.init.call.0)
		*(.init.call.1)
		*(.init.call.2)
		*(.init.call.3)
		*(.init.call.4)
		*(.init.call.5)
		*(.init.call.6)
		*(.init.call.7)
		*(.init.call.8)
		*(.init.call.9)
		*(.init.call)
	}
	_e_init = .;
	. = ALIGN(4K);
	_sdata = .;
	.data :
	{
		*(.boot.data)
		. = ALIGN(4K);
		*(.boot.page)
		*(.boot.log)
		*(.boot.map_util)
		*(.data .data.*)
		. = ALIGN(8);
		*(.user)
	}
	_edata = .;
	. = ALIGN(4K);
	_bss_start = .;
	.bss :
	{
		*(.bss .bss.*)
	}
	_bss_end = .;
	. =ALIGN(4K);
	_per_cpu_start = .;
	.percpu : 
	{
		*(.percpu..data)
	}
	. = ALIGN(64);
	_per_cpu_end = .;
	_end = .;
	/DISCARD/ : {
        *(.comment) *(.gnu*) *(.note*) *(.eh_frame*)
    }
}