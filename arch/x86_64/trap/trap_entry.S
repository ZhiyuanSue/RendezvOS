	.section	.trap
	.global	trap_entry
trap_entry:
	call	trap_handler

	.global trap_exit
trap_exit:
	iretq

