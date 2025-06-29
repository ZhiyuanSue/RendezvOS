	.section	.text
	.global		arch_init_drop_to_user
arch_init_drop_to_user:
	mov		SP,	X0
	sub		SP,SP,#(39*8)
	mov		X0,	SP
	str		X0,	[SP,#8*34]
	str		X1,	[SP,#8*33]

	/*clear other registers*/
	mov		X0, #0
	stp		X0,X0, [SP,#8*0]
	stp		X0,X0, [SP,#8*2]
	stp		X0,X0, [SP,#8*4]
	stp		X0,X0, [SP,#8*6]
	stp		X0,X0, [SP,#8*8]
	stp		X0,X0, [SP,#8*10]
	stp		X0,X0, [SP,#8*12]
	stp		X0,X0, [SP,#8*14]
	stp		X0,X0, [SP,#8*16]
	stp		X0,X0, [SP,#8*18]
	stp		X0,X0, [SP,#8*20]
	stp		X0,X0, [SP,#8*22]
	stp		X0,X0, [SP,#8*24]
	stp		X0,X0, [SP,#8*26]
	stp		X0,X0, [SP,#8*28]
	stp		X0,X0, [SP,#8*30]
	str		X0,	[SP,#8*32]
	str		X0,	[SP,#8*35]
	stp		X0,X0, [SP,#8*36]
	str		X0, [SP,#8*38]

	b		el0_sync_trap_exit