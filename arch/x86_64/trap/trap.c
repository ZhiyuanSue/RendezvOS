#include <arch/x86_64/trap.h>
#include <arch/x86_64/sys_ctrl.h>

extern	u64*	trap_vec;
extern	struct idt_gate_desc	idt[256];
const char* trap_name_string[ARCH_USED_TRAP_NUM+2]={
	"Fault:Divide Error\n\0",
	"Fault/Trap:Debug Exception\n\0",
	"Interrupt:NMI Interrupt\n\0",
	"Trap:Breakpoint\n\0",
	"Trap:Overflow\n\0",
	"Fault:BOUND Range exceeded\n\0",
	"Fault:Invalid Opcode\n\0",
	"Fault:Math Coprocessor Device Not Available\n\0",
	"Abort:Double Fault\n\0",
	"Fault:Coprocessor Segment Overrun\n\0",
	"Fault:Invalid TSS\n\0",
	"Fault:Segment Not Present\n\0",
	"Fault:Stack-Segment Fault\n\0",
	"Fault:General Protection\n\0",
	"Fault:Page Fault\n\0",
	"Intel reserved\n\0",
	"Fault:x87 FPU Floating-Point Error\n\0",
	"Fault:Alignment Check\n\0",
	"Abort:Machine Check\n\0",
	"Fault:SIMD Floating-Point Exception\n\0",
	"Fault:Virtualization Exception\n\0",
	/*others*/
	"Intel reserved\n\0",	/*for trap number between 21-31*/
	"User Defined interrupts\n\0"
};
void	init_idt(void)
{
	struct desc_table_reg_desc idtr_desc;
	idtr_desc.limit=IDT_LIMIT-1;
	idtr_desc.base_addr=(u64)(&idt);
	for(int i=0;i<IDT_LIMIT;++i)
	{
		/*generate the IDT table*/
		;
	}
	lidt(&idtr_desc);
}

void	trap_handler()
{
	
}