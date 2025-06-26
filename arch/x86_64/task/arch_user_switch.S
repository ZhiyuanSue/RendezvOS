/*leave the kernel*/
    .section .text
    .global arch_exit_kernel
arch_exit_kernel:
    /*TODO:add the sysret codes*/
    /* close the irq*/
    cli

    /*set the cs ds and other segment*/
    
    /*restore the gs and fs*/
    swapgs

    /*restore the callee saved registers*/

    /*set the sysret registers*/
    sysretq

/*enter the kernel*/
    .section .text
    .global arch_enter_kernel
arch_enter_kernel:
    /*get the percpu base gs*/
    swapgs

    /*save the user stack to kernel*/
    mov %rsp,$gs:__user_rsp_scratch_offset
    /*read tss and find the kernel stack*/
    mov %gs:__tss_offset,%rsp

    /*save user rsp and rcx and c11*/

    /*change the segment*/

    /*get caller seved registers
    remember that the rcx is taken place by r10*/


