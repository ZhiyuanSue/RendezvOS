#include <arch/aarch64/boot/arch_setup.h>
#include <modules/log/log.h>
#include <modules/dtb/dtb.h>
int	start_arch (struct setup_info* arch_setup_info)
{
	/*parse the dtb*/
	pr_info("dtb phy addr is 0x%x\n",arch_setup_info->dtb_ptr);
	/*we need to map the dtb page to a virt address first*/
	pr_info("map end addr is 0x%x\n",arch_setup_info->map_end_phy_addr);
	return 0;
}