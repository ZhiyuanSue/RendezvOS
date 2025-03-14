.macro el1_trap_entry
    
    sub sp,sp,#(39*8)

    stp x0,x1,[sp,#8*1]
    stp x2,x3,[sp,#8*3]
    stp x4,x5,[sp,#8*5]
    stp x6,x7,[sp,#8*7]
    stp x8,x9,[sp,#8*9]
    stp x10,x11,[sp,#8*11]
    stp x12,x13,[sp,#8*13]
    stp x14,x15,[sp,#8*15]
    stp x16,x17,[sp,#8*17]
    stp x18,x19,[sp,#8*19]
    stp x20,x21,[sp,#8*21]
    stp x22,x23,[sp,#8*23]
    stp x24,x25,[sp,#8*25]
    stp x26,x27,[sp,#8*27]
    stp x28,x29,[sp,#8*29]

    mrs x19,SPSR_EL1
    mrs x20,ELR_EL1
    mov x21,SP
    mrs x22,ESR_EL1
    mrs x23,FAR_EL1

    stp x30,x19,[sp,#8*31]
    stp x20,x21,[sp,#8*33]
    stp x22,x23,[sp,#8*35]
    str x25,[sp,#8*38]

    mov x0,SP
    
.endm

.macro el1_trap_exit

    ldr x25,[sp,#8*38]
    ldp x22,x23,[sp,#8*35]
    ldp x20,x21,[sp,#8*33]
    ldp x30,x19,[sp,#8*31]

    msr FAR_EL1,x23
    msr ESR_EL1,x22
    mov SP,x21
    msr ELR_EL1,x20
    msr SPSR_EL1,x19

    ldp x28,x29,[sp,#8*29]
    ldp x26,x27,[sp,#8*27]
    ldp x24,x25,[sp,#8*25]
    ldp x22,x23,[sp,#8*23]
    ldp x20,x21,[sp,#8*21]
    ldp x18,x19,[sp,#8*19]
    ldp x16,x17,[sp,#8*17]
    ldp x14,x15,[sp,#8*15]
    ldp x12,x13,[sp,#8*13]
    ldp x10,x11,[sp,#8*11]
    ldp x8,x9,[sp,#8*9]
    ldp x6,x7,[sp,#8*7]
    ldp x4,x5,[sp,#8*5]
    ldp x2,x3,[sp,#8*3]
    ldp x0,x1,[sp,#8*1]

    add sp,sp,#(39*8)
    eret
.endm