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
    mov		%rsp,%gs:__user_rsp_scratch_offset
    /*read tss and find the kernel stack*/
    mov		%gs:__tss_offset,%rsp

    /*save user rsp and rcx and c11*/
	/*this data is saved at the kernel stack_bottom*/
	sub		$(10*8),%rsp
	mov		%rcx,	8(%rsp)
	mov		%r11,	2*8(%rsp)

	mov		%r15,	3*8(%rsp)
	mov		%r14,	4*8(%rsp)
	mov		%r13,	5*8(%rsp)
	mov		%r12,	6*8(%rsp)
	mov		%rbp,	7*8(%rsp)
	mov		%rbx,	8*8(%rsp)
	
	mov		%rax,	9*8(%rsp)
	mov		%gs:__user_rsp_scratch_offset,%rax
	mov		%rax,	(%rsp)	
	
    /*change the ds and es segment*/
	mov		$GDT_KERNEL_DS_INDEX,	%rax
	mov		%rax,	%ds
	mov		%rax,	%es

    /*get caller seved registers
    remember that the rcx is taken place by r10,so we need to mov r10 to rcx*/
	mov 	%r10,	%rcx
	mov		9*8(%rsp),	%rax

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
    mov		$GDT_USER_DS_INDEX,	%rax
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
	movabs $arch_enter_kernel,%rax
	mov $MSR_IA32_LSTAR,%rcx
	wrmsr
	ret