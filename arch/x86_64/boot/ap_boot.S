	.section .boot.ap_code
	.p2align 12
	.global ap_start
.code32
ap_start:
	jmp ap_start


    .p2align 3
.code64
x86_64_entry:
    hlt

	.global ap_start_end
ap_start_end:
	hlt