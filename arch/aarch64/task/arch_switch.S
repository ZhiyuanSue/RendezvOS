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

	/* x0 = 16-byte-aligned buffer: Q0–Q31, then FPCR, FPSR (u32 each). */
	.global		arch_fp_save_to
arch_fp_save_to:
	stp	q0,	q1,	[x0, #0]
	stp	q2,	q3,	[x0, #32]
	stp	q4,	q5,	[x0, #64]
	stp	q6,	q7,	[x0, #96]
	stp	q8,	q9,	[x0, #128]
	stp	q10,	q11,	[x0, #160]
	stp	q12,	q13,	[x0, #192]
	stp	q14,	q15,	[x0, #224]
	stp	q16,	q17,	[x0, #256]
	stp	q18,	q19,	[x0, #288]
	stp	q20,	q21,	[x0, #320]
	stp	q22,	q23,	[x0, #352]
	stp	q24,	q25,	[x0, #384]
	stp	q26,	q27,	[x0, #416]
	stp	q28,	q29,	[x0, #448]
	stp	q30,	q31,	[x0, #480]
	mrs	x1,	fpcr
	mrs	x2,	fpsr
	str	w1,	[x0, #512]
	str	w2,	[x0, #516]
	ret

	.global		arch_fp_load_from
arch_fp_load_from:
	ldp	q0,	q1,	[x0, #0]
	ldp	q2,	q3,	[x0, #32]
	ldp	q4,	q5,	[x0, #64]
	ldp	q6,	q7,	[x0, #96]
	ldp	q8,	q9,	[x0, #128]
	ldp	q10,	q11,	[x0, #160]
	ldp	q12,	q13,	[x0, #192]
	ldp	q14,	q15,	[x0, #224]
	ldp	q16,	q17,	[x0, #256]
	ldp	q18,	q19,	[x0, #288]
	ldp	q20,	q21,	[x0, #320]
	ldp	q22,	q23,	[x0, #352]
	ldp	q24,	q25,	[x0, #384]
	ldp	q26,	q27,	[x0, #416]
	ldp	q28,	q29,	[x0, #448]
	ldp	q30,	q31,	[x0, #480]
	ldr	w1,	[x0, #512]
	ldr	w2,	[x0, #516]
	msr	fpcr,	x1
	msr	fpsr,	x2
	ret

	.section	.text
	.global		arch_drop_to_user
arch_drop_to_user:
	mov		SP,	X0
	b		el0_sync_trap_exit