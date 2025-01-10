
.macro trap_entry
	sub		$(15*8),%rsp

	mov		%r15,	(%rsp)
	mov		%r14,	8(%rsp)
	mov		%r13,	16(%rsp)
	mov		%r12,	24(%rsp)
	mov		%rbp,	32(%rsp)
	mov		%rbx,	40(%rsp)
	mov		%r11,	48(%rsp)
	mov		%r10,	56(%rsp)
	mov		%r9,	64(%rsp)
	mov		%r8,	72(%rsp)
	mov		%rax,	80(%rsp)
	mov		%rcx,	88(%rsp)
	mov		%rdx,	96(%rsp)
	mov		%rsi,	104(%rsp)
	mov		%rdi,	112(%rsp)
	
	mov		%rsp,%rdi	/*use the rsp as the trap frame pointer*/
.endm

.macro trap_exit
	mov		112(%rsp),	%rdi
	mov		104(%rsp),	%rsi
	mov		96(%rsp),	%rdx
	mov		88(%rsp),	%rcx
	mov		80(%rsp),	%rax
	mov		72(%rsp),	%r8
	mov		64(%rsp),	%r9
	mov		56(%rsp),	%r10
	mov		48(%rsp),	%r11
	mov		40(%rsp),	%rbx
	mov		32(%rsp),	%rbp
	mov		24(%rsp),	%r12
	mov		16(%rsp),	%r13
	mov		8(%rsp),	%r14
	mov		(%rsp),		%r15

	add		$(15*8),%rsp		//also pop error code
	iretq
.endm
