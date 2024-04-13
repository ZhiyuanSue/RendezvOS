#include <shampoos/common.h>
#include <shampoos/error.h>
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

	/*TODO:clean the bss segment*/
	return 0;
}
