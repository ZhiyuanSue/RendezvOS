#define MSR_FS_BASE        0xC0000100
#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102
#define MSR_IA32_LSTAR	   0xc0000082
#define GDT_KERNEL_DS_INDEX 	2
#define GDT_USER_DS_INDEX		5


/*enter the kernel*/
    .section .text
    .global arch_enter_kernel
arch_enter_kernel:
    /*get the percpu base gs*/
    swapgs

    /*save the user stack to kernel*/
    mov		%rsp,	%gs:__user_rsp_scratch_offset

	/*change the ds and es segment*/
	mov		$GDT_KERNEL_DS_INDEX*8,	%rsp
	mov		%rsp,	%ds
	mov		%rsp,	%es
    
	/*read tss and find the kernel stack*/
    mov		%gs:__tss_rsp_offset,	%rsp

    /*save user rsp and rcx and c11*/
	/*this data is saved at the kernel stack_bottom*/
	sub		$(22*8),%rsp

	mov		%r15,	1*8(%rsp)
	mov		%r14,	2*8(%rsp)
	mov		%r13,	3*8(%rsp)
	mov		%r12,	4*8(%rsp)
	mov		%rbp,	5*8(%rsp)
	mov		%rbx,	6*8(%rsp)
	mov		%r11,	7*8(%rsp)
	
	/*get the syscall info*/
	mov		%r10,	8*8(%rsp)
	mov		%r9,	9*8(%rsp)
	mov		%r8,	10*8(%rsp)
	mov		%rax,	11*8(%rsp)
	mov		%rcx,	12*8(%rsp)
	mov		%rdx,	13*8(%rsp)
	mov		%rsi,	14*8(%rsp)
	mov		%rdi,	15*8(%rsp)

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
	mov		1*8(%rsp),	%r15
	mov		2*8(%rsp),	%r14
	mov		3*8(%rsp),	%r13
	mov		4*8(%rsp),	%r12
	mov		5*8(%rsp),	%rbp
	mov		6*8(%rsp),	%rbx
	mov		7*8(%rsp),	%r11

	mov		12*8(%rsp),	%rcx
	
    /*set the ds and es segment, the cs and ss is set at IA32_STAR_MSR*/
    mov		$GDT_USER_DS_INDEX*8,	%rsp
	mov		%rsp,	%ds
	mov		%rsp,	%es

	/*
		we do not add the kernel rsp
		every time we go into the kernel,
		we use an empty kernel stack
	*/
	mov		%gs:__user_rsp_scratch_offset,		%rsp
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