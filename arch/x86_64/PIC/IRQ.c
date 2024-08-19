
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/sys_ctrl.h>
#include <modules/log/log.h>
#include <shampoos/bit.h>
extern struct cpuinfo_x86 cpuinfo;
void arch_init_irq() {
	u64 APIC_BASE_val;
	if (cpuinfo.feature_2 & X86_CPUID_FEATURE_EDX_APIC) {
		pr_info("use the Local APIC\n");
		pr_info("we disable it\n");
		APIC_BASE_val = rdmsr(IA32_APIC_BASE_addr);
		APIC_BASE_val = clear_mask(APIC_BASE_val, IA32_APIC_BASE_X_ENABLE);
		wrmsr(IA32_APIC_BASE_addr, APIC_BASE_val);
	} else
		pr_info("use 8259A\n");
	if (cpuinfo.feature_1 & X86_CPUID_FEATURE_ECX_x2APIC) {
		pr_info("support x2APIC\n");
	} else
		pr_info("no x2APIC support\n");
}