#include <common/assemble.h>
#include "./trap_entry.S"

BEGIN_FUNC(trap_vec_table)
	.p2align 11
	b	unexpected_trap	/*current EL with SP_0*/
	.p2align 7
	b	unexpected_trap
	.p2align 7
	b	unexpected_trap
	.p2align 7
	b	unexpected_trap
	
	.p2align 7
	b	curr_el_sync	/*current EL with SP_x*/
	.p2align 7
	b	curr_el_irq
	.p2align 7
	b	curr_el_fiq
	.p2align 7
	b	unexpected_trap
	
	.p2align 7	/*Lower EL with AArch64*/
	b	low_el_sync
	.p2align 7
	b	low_el_irq
	.p2align 7
	b	low_el_fiq
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

BEGIN_FUNC(curr_el_sync)
	el1_trap_entry
	bl get_curr_el_trap_info
	mov x0,SP
	bl trap_handler
	el1_trap_exit
END_FUNC(curr_el_sync)

BEGIN_FUNC(curr_el_irq)
	el1_trap_entry
	bl get_curr_el_trap_info
	mov x0,SP
	bl trap_handler
	el1_trap_exit
END_FUNC(curr_el_irq)

BEGIN_FUNC(curr_el_fiq)
	el1_trap_entry
	bl get_curr_el_trap_info
	mov x0,SP
	bl trap_handler
	el1_trap_exit
END_FUNC(curr_el_fiq)

/*trap entry with lower el*/

BEGIN_FUNC(low_el_sync)
	bl trap_handler
END_FUNC(low_el_sync)

BEGIN_FUNC(low_el_irq)
	bl trap_handler
END_FUNC(low_el_irq)

BEGIN_FUNC(low_el_fiq)
	bl trap_handler
END_FUNC(low_el_fiq)

BEGIN_FUNC(unexpected_trap)
	bl arch_unknown_trap_handler
END_FUNC(unexpected_trap)
