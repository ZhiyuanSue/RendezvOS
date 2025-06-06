    .section    .text
    .global     run_thread
run_thread:
    // save the fp and the lr
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
	// load the parameter
    ldr x9, [x0,#0];
    ldp x6, x7, [x0,#8*7]
    ldp x4, x5, [x0,#8*5]
    ldp x2, x3, [x0,#8*3]
    ldp x0, x1, [x0,#8*1]

    blr x9
    ldp  x29, x30, [sp], #16
    ret