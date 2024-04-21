#include <arch/x86_64/sys_ctrl.h>
#include <shampoos/common.h>
#include <shampoos/error.h>
static void enable_cache()
{
}
static void start_fp()
{
	set_cr0_bit(CR0_MP);
	set_cr0_bit(CR0_NE);
}
static void start_simd()
{
	struct cpuid_result tmp_result;
	u32 cpuid_func;
	cpuid_func=0x01;
	/*use cpuid to check the simd support or not*/
	tmp_result=cpuid(cpuid_func);
	/*pr_info("cpuid result is 0x%x,0x%x,0x%x,0x%x\n",tmp_result.eax,tmp_result.ebx,tmp_result.ecx,tmp_result.edx);*/
	if( ((tmp_result.edx) & ( (1<<25)|(1<<26)|(1<<24)|(1<<19) )) && \
		((tmp_result.ecx) & ( (1<<0)|(1<<9) )) 
		)
	{
		pr_info("have simd feature\n");
		/*set osfxsr : cr4 bit 9*/
		set_cr4_bit(CR4_OSFXSR);
		/*set osxmmexcpt flag: cr4 bit 10*/
		set_cr4_bit(CR4_OSXMMEXCPT);
		/*set the mask bit and flags in mxcsr register*/
		//set_mxcsr( MXCSR_IE|MXCSR_DE|MXCSR_ZE|MXCSR_OE|MXCSR_UE|MXCSR_PE|	\
		//		MXCSR_IM|MXCSR_DM|MXCSR_ZM|MXCSR_OM|MXCSR_UM|MXCSR_PM );
	}
	else{
		goto start_simd_fail;
	}
	return;
start_simd_fail:
	pr_error("start simd fail\n");
}
int start_arch (struct setup_info* arch_setup_info)
{
	u32 mtb_magic=arch_setup_info->multiboot_magic;
	struct	multiboot_info*	mtb_info=GET_MULTIBOOT_INFO(arch_setup_info);
	if(mtb_magic!=MULTIBOOT_MAGIC)
	{
		pr_info("not using the multiboot protocol, stop\n");
		return -EPERM;
	}
	pr_info("finish check the magic:%x\n",mtb_magic);
	if(!(mtb_info->flags & MULTIBOOT_INFO_FLAG_CMD)){
		pr_info("cmdline:%s\n",(char*)(mtb_info->cmdline+KERNEL_VIRT_OFFSET));
	}
	else{
		pr_info("no input cmdline\n");
	}
	enable_cache();
	start_fp();
	start_simd();
	return 0;
}
