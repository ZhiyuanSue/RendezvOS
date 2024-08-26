#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/time.h>
#include <arch/x86_64/trap.h>
#include <modules/driver/timer/8254.h>
#include <modules/log/log.h>
#include <shampoos/error.h>

extern u32			max_phy_addr_width;
struct cpuinfo_x86	cpuinfo;
static void	get_cpuinfo(void)
{
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	/*TODO :rewite the check of cpuid*/
	/*first get the number that cpuid support*/
	cpuid(0x0, &eax, &ebx, &ecx, &edx);
	cpuid(0x1, &eax, &ebx, &ecx, &edx);
	cpuinfo.feature_1 = ecx;
	cpuinfo.feature_2 = edx;
}
static void	enable_cache(void)
{
}
static void	start_fp(void)
{
	set_cr0_bit(CR0_MP);
	set_cr0_bit(CR0_NE);
}
static void	start_simd(void)
{
	u64	xcr_value;

	/*use cpuinfo to check the simd support or not*/
	if (((cpuinfo.feature_2) & (X86_CPUID_FEATURE_EDX_SSE | X86_CPUID_FEATURE_EDX_SSE2 | X86_CPUID_FEATURE_EDX_FXSR | X86_CPUID_FEATURE_EDX_CLFSH))
		&& ((cpuinfo.feature_1) & (X86_CPUID_FEATURE_ECX_SSE3 | X86_CPUID_FEATURE_ECX_SSSE3)))
	{
		pr_info("have simd feature,starting...\n");
		/*set osfxsr : cr4 bit 9*/
		set_cr4_bit(CR4_OSFXSR);
		/*set osxmmexcpt flag: cr4 bit 10*/
		set_cr4_bit(CR4_OSXMMEXCPT);
		/*set the mask bit and flags in mxcsr register*/
		set_mxcsr(MXCSR_IM | MXCSR_DM | MXCSR_ZM | MXCSR_OM | MXCSR_UM | MXCSR_PM);
		/*the following codes seems useless for enable sse,emmm*/
		if ((cpuinfo.feature_1) & X86_CPUID_FEATURE_ECX_XSAVE)
		{
			/*to enable the xcr0, must set the cr4 osxsave*/
			set_cr4_bit(CR4_OSXSAVE);
			xcr_value = get_xcr(0);
			set_xcr(0, xcr_value | XCR0_X87 | XCR0_SSE | XCR0_AVX);
		}
	}
	else
	{
		goto start_simd_fail;
	}
	return ;
start_simd_fail:
	pr_error("start simd fail\n");
}
error_t	start_arch(struct setup_info *arch_setup_info)
{
	u32						mtb_magic;
	struct multiboot_info	*mtb_info;

	mtb_magic = arch_setup_info->multiboot_magic;
	mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
	if (mtb_magic != MULTIBOOT_MAGIC)
	{
		pr_info("not using the multiboot protocol, stop\n");
		return (-EPERM);
	}
	pr_info("finish check the magic:%x\n", mtb_magic);
	max_phy_addr_width = arch_setup_info->phy_addr_width;
	if (!(mtb_info->flags & MULTIBOOT_INFO_FLAG_CMD))
	{
		pr_info("cmdline:%s\n", (char *)(mtb_info->cmdline
				+ KERNEL_VIRT_OFFSET));
	}
	else
	{
		pr_info("no input cmdline\n");
	}
	get_cpuinfo();
	init_interrupt();
	init_irq();
	init_timer();
	enable_cache();
	start_fp();
	start_simd();
	return (0);
}
