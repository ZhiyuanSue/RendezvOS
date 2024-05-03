/* some definations */
	.set	kernel_virt_offset,0xffff000000000000
	

	.section	.boot
_start:
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
