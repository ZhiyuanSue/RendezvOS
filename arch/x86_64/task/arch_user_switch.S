#define MSR_FS_BASE        0xC0000100
#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102
#define MSR_IA32_LSTAR	   0xc0000082
#define GDT_KERNEL_DS_INDEX 	2
#define GDT_USER_DS_INDEX		5

/*first time drop to the user*/
	.section .text
	.global arch_init_drop_to_user
arch_init_drop_to_user:
	cli
	mov		%rdi,	%rsp
	sub		$(10*8),%rsp
	mov		%rsi,	(%rsp)
	mov		%rdx,	8(%rsp)

	mov		$0,		%rax
	mov		%rax,	2*8(%rsp)
	mov		%rax,	3*8(%rsp)
	mov		%rax,	4*8(%rsp)
	mov		%rax,	5*8(%rsp)
	mov		%rax,	6*8(%rsp)
	mov		%rax,	7*8(%rsp)
	mov		%rax,	8*8(%rsp)
	mov		%rax,	9*8(%rsp)

	/*clean other registers*/
	mov		%rax,	%r10
	mov		%rax,	%r9
	mov		%rax,	%r8
	mov		%rax,	%rdx
	mov		%rax,	%rsi
	mov		%rax,	%rdi
	jmp arch_exit_kernel

/*enter the kernel*/
    .section .text
    .global arch_enter_kernel
arch_enter_kernel:
    /*get the percpu base gs*/
    swapgs

    /*save the user stack to kernel*/
    mov		%rsp,	%gs:__user_rsp_scratch_offset
    /*read tss and find the kernel stack*/
    mov		%gs:__tss_rsp_offset,	%rsp
	mov		%rax,	%gs:__tmp_rax_offset

	/*change the ds and es segment*/
	mov		$GDT_KERNEL_DS_INDEX*8,	%rax
	mov		%rax,	%ds
	mov		%rax,	%es

    /*save user rsp and rcx and c11*/
	/*this data is saved at the kernel stack_bottom*/
	sub		$(16*8),%rsp
	
	mov		%gs:__user_rsp_scratch_offset,%rax
	mov		%rax,	(%rsp)	
	mov		%rcx,	8(%rsp)
	mov		%r11,	2*8(%rsp)

	mov		%r15,	3*8(%rsp)
	mov		%r14,	4*8(%rsp)
	mov		%r13,	5*8(%rsp)
	mov		%r12,	6*8(%rsp)
	mov		%rbp,	7*8(%rsp)
	mov		%rbx,	8*8(%rsp)
	
	/*get the syscall info*/
	mov		%gs:__tmp_rax_offset, %rax	
	mov		%rax,	9*8(%rsp)
	mov		%rdi,	10*8(%rsp)
	mov		%rsi,	11*8(%rsp)
	mov		%rdx,	12*8(%rsp)
	mov		%r10,	13*8(%rsp)
	mov		%r8,	14*8(%rsp)
	mov		%r9,	15*8(%rsp)

    /*give a syscall_context to the syscall handler*/
	mov		%rsp,	%rdi

	/*open the irq*/
	sti

	/*call the syscall entey*/
	call syscall


/*leave the kernel,rax is the return value*/
    .section .text
    .global arch_exit_kernel
arch_exit_kernel:
    /* close the irq*/
    cli

    /*restore the callee saved registers*/
	mov		%rax,	9*8(%rsp)
	mov		8*8(%rsp),	%rbx
	mov		7*8(%rsp),	%rbp
	mov		6*8(%rsp),	%r12
	mov		5*8(%rsp),	%r13
	mov		4*8(%rsp),	%r14
	mov		3*8(%rsp),	%r15
	mov		2*8(%rsp),	%r11
	mov		8(%rsp),	%rcx
	
    /*set the ds and es segment, the cs and ss is set at IA32_STAR_MSR*/
    mov		$GDT_USER_DS_INDEX*8,	%rax
	mov		%rax,	%ds
	mov		%rax,	%es

	/*restore the rax*/
	mov		9*8(%rsp),	%rax
	/*
		we do not add the kernel rsp
		every time we go into the kernel,
		we use an empty kernel stack
	*/
	mov		(%rsp),		%rsp
    /*restore the gs and fs*/
    swapgs
    sysretq

	.section .text
	.global arch_set_syscall_entry
arch_set_syscall_entry:
	movabs	$arch_enter_kernel,%rax
	mov		%rax,%rdx
	shr		$32,%rdx
	mov		$MSR_IA32_LSTAR,%rcx
	wrmsr
	ret