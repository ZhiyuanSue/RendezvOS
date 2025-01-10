
.macro trap_entry
	sub		$(15*8),	%rsp

	mov		(%rsp),		%r15
	mov		8(%rsp),	%r14
	mov		16(%rsp),	%r13
	mov		24(%rsp),	%r12
	mov		32(%rsp),	%rbp
	mov		40(%rsp),	%rbx
	mov		48(%rsp),	%r11
	mov		56(%rsp),	%r10
	mov		64(%rsp),	%r9
	mov		72(%rsp),	%r8
	mov		80(%rsp),	%rax
	mov		88(%rsp),	%rcx
	mov		96(%rsp),	%rdx
	mov		104(%rsp),	%rsi
	mov		112(%rsp),	%rdi
	
	mov		%rdi,%rsp	/*use the rsp as the trap frame pointer*/
.endm

.macro trap_exit
	mov		%rdi,	112(%rsp)
	mov		%rsi,	104(%rsp)
	mov		%rdx,	96(%rsp)
	mov		%rcx,	88(%rsp)
	mov		%rax,	80(%rsp)
	mov		%r8,	72(%rsp)
	mov		%r9,	64(%rsp)
	mov		%r10,	56(%rsp)
	mov		%r11,	48(%rsp)
	mov		%rbx,	40(%rsp)
	mov		%rbp,	32(%rsp)
	mov		%r12,	24(%rsp)
	mov		%r13,	16(%rsp)
	mov		%r14,	8(%rsp)
	mov		%r15,	(%rsp)

	add		$(16*8),		%rsp	//also pop error code
	iretq
.endm
