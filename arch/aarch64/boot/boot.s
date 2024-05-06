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
	/*save some info:x0(dtb),mpidr_el1*/
	adr x4,setup_info
	stp	x0,x1,[x4]
	add x4,x4,#16
	stp x2,x3,[x4]
	/*switch to el1*/
	mrs x0,CurrentEL
	
	/*init some page table info*/

	/*init mmu and some registers*/

	/*clear bss segment*/

	/*set boot stack pointer*/

	/*call cmain*/
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
