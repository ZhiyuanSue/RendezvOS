    .section    .text
    .global     run_thread
run_thread:
	// load the parameter
	mov (%rdi),		%rax

	mov 48(%rdi),	%r9
	mov 40(%rdi),	%r8
	mov 32(%rdi),	%rcx
	mov 24(%rdi),	%rdx
	mov 16(%rdi),	%rsi
	mov 8(%rdi),	%rdi

	call *%rax