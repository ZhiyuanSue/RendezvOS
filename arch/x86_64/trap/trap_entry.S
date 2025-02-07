
.macro trap_entry trap_id
	sub		$(17*8),%rsp

	mov		%r15,	8(%rsp)
	mov		%r14,	16(%rsp)
	mov		%r13,	24(%rsp)
	mov		%r12,	32(%rsp)
	mov		%rbp,	40(%rsp)
	mov		%rbx,	48(%rsp)
	mov		%r11,	56(%rsp)
	mov		%r10,	64(%rsp)
	mov		%r9,	72(%rsp)
	mov		%r8,	80(%rsp)
	mov		%rax,	88(%rsp)
	mov		%rcx,	96(%rsp)
	mov		%rdx,	104(%rsp)
	mov		%rsi,	112(%rsp)
	mov		%rdi,	120(%rsp)

	mov		$(\trap_id),	%rax
	mov		%rax,	(%rsp)
	
	mov		%rsp,%rdi	/*use the rsp as the trap frame pointer*/
.endm

.macro trap_exit
	mov		120(%rsp),	%rdi
	mov		112(%rsp),	%rsi
	mov		104(%rsp),	%rdx
	mov		96(%rsp),	%rcx
	mov		88(%rsp),	%rax
	mov		80(%rsp),	%r8
	mov		72(%rsp),	%r9
	mov		64(%rsp),	%r10
	mov		56(%rsp),	%r11
	mov		48(%rsp),	%rbx
	mov		40(%rsp),	%rbp
	mov		32(%rsp),	%r12
	mov		24(%rsp),	%r13
	mov		16(%rsp),	%r14
	mov		8(%rsp),		%r15

	add		$(17*8),%rsp		//also pop error code
	iretq
.endm
