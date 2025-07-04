#include <common/assemble.h>
#include <arch/aarch64/trap/trap_def.h>
#include "./kernel_entry.S"

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
	el1_trap_entry TRAP_TYPE_SYNC
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	el1_trap_exit
END_FUNC(curr_el_sync)

BEGIN_FUNC(curr_el_irq)
	el1_trap_entry TRAP_TYPE_IRQ
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	el1_trap_exit
END_FUNC(curr_el_irq)

BEGIN_FUNC(curr_el_fiq)
	el1_trap_entry TRAP_TYPE_FIQ
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	el1_trap_exit
END_FUNC(curr_el_fiq)

/*=== === === === === === trap entry with lower el === === === === === ===*/

BEGIN_FUNC(low_el_sync)
	el0_trap_entry TRAP_TYPE_SYNC
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	.global el0_sync_trap_exit
el0_sync_trap_exit:
	el0_trap_exit
END_FUNC(low_el_sync)

BEGIN_FUNC(low_el_irq)
	el0_trap_entry TRAP_TYPE_IRQ
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	el0_trap_exit
END_FUNC(low_el_irq)

BEGIN_FUNC(low_el_fiq)
	el0_trap_entry TRAP_TYPE_FIQ
	bl 	get_curr_el_trap_info
	mov x0,SP
	bl 	trap_handler
	el0_trap_exit
END_FUNC(low_el_fiq)

/*no need to store context, for it must be kernel panic*/
BEGIN_FUNC(unexpected_trap)
	bl arch_unknown_trap_handler
END_FUNC(unexpected_trap)
