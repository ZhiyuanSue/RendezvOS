/*first time drop to the user*/
	.section .text
	.global arch_init_drop_to_user
arch_init_drop_to_user:
	cli
	mov		%rdi,	%rsp
	sub		$(22*8),%rsp
	mov		%rsi,	%gs:__user_rsp_scratch_offset
	mov		%rdx,	12*8(%rsp)

	mov		$0,		%rax
	mov		%rax,	1*8(%rsp)
	mov		%rax,	2*8(%rsp)
	mov		%rax,	3*8(%rsp)
	mov		%rax,	4*8(%rsp)
	mov		%rax,	5*8(%rsp)
	mov		%rax,	6*8(%rsp)
	mov		%rax,	7*8(%rsp)

	/*clean other registers*/
	mov		%rax,	%r10
	mov		%rax,	%r9
	mov		%rax,	%r8
	mov		%rax,	%rdx
	mov		%rax,	%rsi
	mov		%rax,	%rdi
	jmp arch_exit_kernel