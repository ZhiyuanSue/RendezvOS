/* some definations */
	.set	kernel_virt_offset,0xffff800000000000


	.section	.boot
	.global	_start
_start:
_boot_header:
	b _entry
	b _entry
	.quad	0x80000
	.quad	0	/*image size*/
	.quad	0	/*flags*/
	.quad	0
	.quad	0
	.quad	0
	.long	0x644d5241
	.long	0
_entry:
	adr x4,setup_info
	str	x0,[x4]
	b _start



	.section .boot.data
	.global setup_info
setup_info:
	.quad	0
	.quad	log_buffer

	.section .boot.log
	.global log_buffer
log_buffer:
	.zero 0x10000
