#include <arch/x86_64/trap.h>
#include <arch/x86_64/sys_ctrl.h>

extern	u64*	trap_vec;
extern	struct idt_gate_desc	idt[256];
void	init_idt(void)
{
	struct desc_table_reg_desc idtr_desc;
	idtr_desc.limit=IDT_LIMIT-1;
	idtr_desc.base_addr=(u64)(&idt);
	lidt(&idtr_desc);
}

void	trap_handler()
{
	
}