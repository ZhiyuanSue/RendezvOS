#include <arch/x86_64/boot/multiboot.h>
#include <shampoos/common.h>
extern u32 multiboot_info_struct;

void cpu_idle()
{
	while(1);
}