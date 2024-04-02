#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <modules/log/log.h>
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* info = \
		(struct multiboot_info*)((u64)(arch_setup_info->multiboot_info_struct_ptr)+KERNEL_VIRT_OFFSET);
	pr_info("the mem lower is 0x%x ,the upper is 0x%x",info->mem.mem_lower,info->mem.mem_upper);
	return;
}
