#include <common/assemble.h>

BEGIN_FUNC(trap_vec_table)
	.p2align 11
	b	sync	/*current EL with SP_0*/
	.p2align 7
	b	irq
	.p2align 7
	b	fiq
	.p2align 7
	b	unexpected_trap
	
	.p2align 7
	b	sync	/*current EL with SP_x*/
	.p2align 7
	b	irq
	.p2align 7
	b	fiq
	.p2align 7
	b	unexpected_trap
	
	.p2align 7	/*Lower EL with AArch64*/
	b	sync
	.p2align 7
	b	irq
	.p2align 7
	b	fiq
	.p2align 7
	b	unexpected_trap
	
	.p2align 7	/*Lower EL with AArch32*/
	b	unexpected_trap
	.p2align 7
	b	unexpected_trap
	.p2align 7
	b	unexpected_trap
	.p2align 7
	b	unexpected_trap
END_FUNC(trap_vec_table)

BEGIN_FUNC(sync)

END_FUNC(sync)

BEGIN_FUNC(irq)

END_FUNC(irq)

BEGIN_FUNC(fiq)

END_FUNC(fiq)

BEGIN_FUNC(unexpected_trap)

END_FUNC(unexpected_trap)
