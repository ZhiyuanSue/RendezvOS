#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/sys_ctrl.h>
#include <modules/log/log.h>
#include <shampoos/bit.h>

extern struct cpuinfo_x86	cpuinfo;
int							arch_irq_type = NO_IRQ;
static inline bool	xAPIC_support(void)
{
	return (cpuinfo.feature_2 & X86_CPUID_FEATURE_EDX_APIC);
}
static inline bool	x2APIC_support(void)
{
	return (cpuinfo.feature_1 & X86_CPUID_FEATURE_ECX_x2APIC);
}
static inline void	disable_xAPIC(void)
{
	u64	APIC_BASE_val;

	APIC_BASE_val = rdmsr(IA32_APIC_BASE_addr);
	APIC_BASE_val = clear_mask(APIC_BASE_val, IA32_APIC_BASE_X_ENABLE);
	wrmsr(IA32_APIC_BASE_addr, APIC_BASE_val);
}
void	init_irq(void)
{
	if (xAPIC_support())
	{
		if (x2APIC_support())
		{
			pr_info("support and use x2APIC\n");
			arch_irq_type = x2APIC_IRQ;
			disable_PIC();
		}
		else
		{
			pr_info("no x2APIC support and we use the Local APIC\n");
			arch_irq_type = xAPIC_IRQ;
			disable_PIC();
		}
	}
	else
	{
		pr_info("use 8259A\n");
		arch_irq_type = PIC_IRQ;
		init_PIC();
	}
}