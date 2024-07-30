	.section	.trap
	.global	trap_entry
trap_entry:
	call	trap_handler

trap_exit:
	iretq

