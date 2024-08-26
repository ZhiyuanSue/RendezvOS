#ifdef _X86_64_
# include <arch/x86_64/PIC/IRQ.h>
# include <arch/x86_64/time.h>

extern int	arch_irq_type;
void	init_timer(void)
{
	if (arch_irq_type == NO_IRQ)
	{
		return ;
	}
	else if (arch_irq_type == PIC_IRQ)
	{
		init_8254();
		enable_IRQ(_8259A_MASTER_IRQ_NUM_ + _8259A_TIMER_);
	}
	else if (arch_irq_type == xAPIC_IRQ)
	{
		;
	}
	else if (arch_irq_type == x2APIC_IRQ)
	{
		;
	}
}
#endif