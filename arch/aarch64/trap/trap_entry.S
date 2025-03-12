.macro trap_entry trap_id
    
    stp x0,x1,[sp,#8]
    stp x2,x3,[sp,#24]
    stp x4,x5,[sp,#40]
    stp x6,x7,[sp,#56]
    stp x8,x9,[sp,#72]
    stp x10,x11,[sp,#88]
    stp x12,x13,[sp,#104]
    stp x14,x15,[sp,#120]
    stp x16,x17,[sp,#136]
    stp x18,x19,[sp,#152]
    stp x20,x21,[sp,#168]
    stp x22,x23,[sp,#184]
    stp x24,x25,[sp,#200]
    stp x26,x27,[sp,#216]
    stp x28,x29,[sp,#232]

    sub sp,sp,#(39*8)
    mov x0,sp
    
.endm

.macro trap_exit

    eret
.endm