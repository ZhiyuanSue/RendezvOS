#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/trap.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
extern u32 max_phy_addr_width;
static void enable_cache() {}
static void start_fp() {
	set_cr0_bit(CR0_MP);
	set_cr0_bit(CR0_NE);
}
static void start_simd() {
	struct cpuid_result tmp_result;
	u32 cpuid_func;
	u64 xcr_value;
	cpuid_func = 0x01;
	/*use cpuid to check the simd support or not*/
	tmp_result = cpuid(cpuid_func);
	/*pr_info("cpuid result is
	 * 0x%x,0x%x,0x%x,0x%x\n",tmp_result.eax,tmp_result.ebx,tmp_result.ecx,tmp_result.edx);*/
	if (((tmp_result.edx) & ((1 << 25) | (1 << 26) | (1 << 24) | (1 << 19))) &&
		((tmp_result.ecx) & ((1 << 0) | (1 << 9)))) {
		pr_info("have simd feature,starting...\n");
		/*set osfxsr : cr4 bit 9*/
		set_cr4_bit(CR4_OSFXSR);
		/*set osxmmexcpt flag: cr4 bit 10*/
		set_cr4_bit(CR4_OSXMMEXCPT);
		/*set the mask bit and flags in mxcsr register*/
		set_mxcsr(MXCSR_IM | MXCSR_DM | MXCSR_ZM | MXCSR_OM | MXCSR_UM |
				  MXCSR_PM);
		/*the following codes seems useless for enable sse,emmm*/
		if ((tmp_result.ecx) & (1 << 26)) {
			/*to enable the xcr0, must set the cr4 osxsave*/
			set_cr4_bit(CR4_OSXSAVE);
			xcr_value = get_xcr(0);
			set_xcr(0, xcr_value | XCR0_X87 | XCR0_SSE | XCR0_AVX);
		}
	} else {
		goto start_simd_fail;
	}
	return;
start_simd_fail:
	pr_error("start simd fail\n");
}
error_t start_arch(struct setup_info *arch_setup_info) {
	u32 mtb_magic = arch_setup_info->multiboot_magic;
	struct multiboot_info *mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
	if (mtb_magic != MULTIBOOT_MAGIC) {
		pr_info("not using the multiboot protocol, stop\n");
		return -EPERM;
	}
	pr_info("finish check the magic:%x\n", mtb_magic);
	max_phy_addr_width = arch_setup_info->phy_addr_width;
	if (!(mtb_info->flags & MULTIBOOT_INFO_FLAG_CMD)) {
		pr_info("cmdline:%s\n",
				(char *)(mtb_info->cmdline + KERNEL_VIRT_OFFSET));
	} else {
		pr_info("no input cmdline\n");
	}
	init_interrupt();
	enable_cache();
	start_fp();
	start_simd();
	return 0;
}
