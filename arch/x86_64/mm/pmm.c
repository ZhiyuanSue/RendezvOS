#include <arch/x86_64/mm/pmm.h>
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* mtb_info = \
		GET_MULTIBOOT_INFO(arch_setup_info);
	if( MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MEM) )
	{
		pr_info("the mem lower is 0x%x ,the upper is 0x%x\n",mtb_info->mem.mem_lower,mtb_info->mem.mem_upper);
	}else{
		pr_info("no mem info\n");
	}
	if(MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MMAP))
	{
		pr_info("multiboot have mmap\n");
	}
	return;
}