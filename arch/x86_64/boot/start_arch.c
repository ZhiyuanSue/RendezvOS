#include <shampoos/common.h>
#include <shampoos/error.h>
int start_arch (struct setup_info* arch_setup_info)
{
	u32 magic=arch_setup_info->multiboot_magic;
	if(magic!=MULTIBOOT_MAGIC)
		return -EPERM;
	struct multiboot_info* info = \
		(struct multiboot_info*)((u64)(arch_setup_info->multiboot_info_struct_ptr)+KERNEL_VIRT_OFFSET);
	return 0;
}