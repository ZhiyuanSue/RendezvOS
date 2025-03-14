.macro el1_trap_entry trap_type
    
    sub SP,SP,#(39*8)

    stp x0,x1,[SP,#8*1]
    stp x2,x3,[SP,#8*3]
    stp x4,x5,[SP,#8*5]
    stp x6,x7,[SP,#8*7]
    stp x8,x9,[SP,#8*9]
    stp x10,x11,[SP,#8*11]
    stp x12,x13,[SP,#8*13]
    stp x14,x15,[SP,#8*15]
    stp x16,x17,[SP,#8*17]
    stp x18,x19,[SP,#8*19]
    stp x20,x21,[SP,#8*21]
    stp x22,x23,[SP,#8*23]
    stp x24,x25,[SP,#8*25]
    stp x26,x27,[SP,#8*27]
    stp x28,x29,[SP,#8*29]

    mrs x19,SPSR_EL1
    mrs x20,ELR_EL1
    mov x21,SP
    mrs x22,ESR_EL1
    mrs x23,FAR_EL1
	mov x0,#(\trap_type)

	str x0,[SP]
    stp x30,x19,[SP,#8*31]
    stp x20,x21,[SP,#8*33]
    stp x22,x23,[SP,#8*35]
    str x25,[SP,#8*38]

    mov x0,SP
    
.endm

.macro el1_trap_exit

    ldr x25,[SP,#8*38]
    ldp x22,x23,[SP,#8*35]
    ldp x20,x21,[SP,#8*33]
    ldp x30,x19,[SP,#8*31]

    msr FAR_EL1,x23
    msr ESR_EL1,x22
    mov SP,x21
    msr ELR_EL1,x20
    msr SPSR_EL1,x19

    ldp x28,x29,[SP,#8*29]
    ldp x26,x27,[SP,#8*27]
    ldp x24,x25,[SP,#8*25]
    ldp x22,x23,[SP,#8*23]
    ldp x20,x21,[SP,#8*21]
    ldp x18,x19,[SP,#8*19]
    ldp x16,x17,[SP,#8*17]
    ldp x14,x15,[SP,#8*15]
    ldp x12,x13,[SP,#8*13]
    ldp x10,x11,[SP,#8*11]
    ldp x8,x9,[SP,#8*9]
    ldp x6,x7,[SP,#8*7]
    ldp x4,x5,[SP,#8*5]
    ldp x2,x3,[SP,#8*3]
    ldp x0,x1,[SP,#8*1]

    add SP,SP,#(39*8)
    eret
.endm