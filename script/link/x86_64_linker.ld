ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
SECTIONS {
	kernel_virt_offset = 0xffff800000000000 ;
	kernel_start_offset = 0x100000 ;
	. = kernel_virt_offset + kernel_start_offset;
	_kstart = .;
	_start = .;
	.text : 
	{
		*(.boot)
		*(.boot_64)
		*(.boot.*)
		*(.text .text.*)
		*(.trap .trap.*)
	}
	. = ALIGN(4K);
	_sdata = .;
	.data :
	{
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
		*(.boot_stack)
		_boot_stack_bottom = .;
		*(.bss .bss.*)
	}
	_bss_end = .;
	. =ALIGN(4K);
	_per_cpu_start = .;
	.percpu : 
	{
		*(.percpu..data)
		__tss_offset = cpu_tss - _per_cpu_start;
		__user_rsp_scratch_offset = user_rsp_scratch - _per_cpu_start;
	}
	. = ALIGN(64);
	_per_cpu_end = .;
	_end = .;
	/DISCARD/ : {
        *(.comment) *(.gnu*) *(.note*) *(.eh_frame*)
    }
}
