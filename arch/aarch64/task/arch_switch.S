    .section    .text
    .global     context_switch
context_switch:
	// save the old
	
	stp	x19,		x20,		[x0,#8*2]
	stp	x21,		x22,		[x0,#8*4]
	stp	x23,		x24,		[x0,#8*6]
	stp	x25,		x26,		[x0,#8*8]
	stp	x27,		x28,		[x0,#8*10]
	stp	x29,		x30,		[x0,#8*12]
	mov	x19,		sp
	mrs	x20,		spsr_el1
	stp	x19,		x20,		[x0,#0]
	// load the new

	ldp	x19,		x20,			[x1,#0]
	msr	spsr_el1,	x20
	mov	sp,			x19
	ldp x29,		x30,		[x1,#8*12]
	ldp	x27,		x28,		[x1,#8*10]
	ldp	x25,		x26,		[x1,#8*8]
	ldp	x23,		x24,		[x1,#8*6]
	ldp	x21,		x22,		[x1,#8*4]
	ldp	x19,		x20,		[x1,#8*2]
    // return
    ret