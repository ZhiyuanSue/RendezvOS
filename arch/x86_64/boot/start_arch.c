#include <shampoos/common.h>
#include <shampoos/error.h>
int start_arch (struct setup_info* arch_setup_info)
{
	u32 magic=arch_setup_info->multiboot_magic;
	if(magic!=MULTIBOOT_MAGIC)
		return -EPERM;
	pr_info("finish check the magic:%x\n",magic);
	return 0;
}
