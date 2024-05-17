#include <arch/aarch64/boot/arch_setup.h>
#include <arch/aarch64/power_ctrl.h>
#include <modules/log/log.h>
#include <modules/dtb/dtb.h>
#include <common/mm.h>
void	try_map_dtb(struct fdt_header* dtb_header_ptr,struct setup_info* arch_setup_info)
{
	/*calculate the dtb size
		case 1:if the dtb size is less or equal than 4k,do nothing
		case 2:if the left space of this 2m entry can store the left part of this dtb
		we should map the left pages to it
		case 3:if the left space is larger then 2m, but less then the 1G - ( map_end - round_down(map_end,1G) )
		we should map the left pages with 2m pages,and we also remap the header
		case 4:if not the upper case, which means that the dtb file is too large or cross the 1G boundary
		we just generate an error
		after that we should update the dtb base addr if necessary
	*/
	u32	dtb_size=dtb_header_ptr->totalsize;
	pr_info("dtb size is 0x%x\n",dtb_size);
	if(dtb_size<PAGE_SIZE)	/*case 1*/
		return;
	u32	unmapped_size=dtb_size-PAGE_SIZE;
	u32	left_space_of_2m_entry	=	ROUND_UP(((u64)dtb_header_ptr+PAGE_SIZE),MIDDLE_PAGE_SIZE) - (u64)dtb_header_ptr;
	u32	left_space_of_1g_entry	=	ROUND_UP(((u64)dtb_header_ptr+PAGE_SIZE),HUGE_PAGE_SIZE) - ROUND_UP(((u64)dtb_header_ptr+PAGE_SIZE),MIDDLE_PAGE_SIZE);
	if(unmapped_size<=left_space_of_2m_entry){	/*case 2*/

		return;
	}else if(dtb_size<=left_space_of_1g_entry){	/*case 3*/

		arch_setup_info->boot_dtb_header_base_addr=ROUND_UP(((u64)dtb_header_ptr+PAGE_SIZE),MIDDLE_PAGE_SIZE);
		return;
	}
	/*case 4*/
	pr_error("error at map dtb\n");
	arch_shutdown();
}
int	start_arch (struct setup_info* arch_setup_info)
{
	
	pr_info("dtb phy addr is 0x%x\n",arch_setup_info->dtb_ptr);
	/*we need to map the dtb page to a virt address first*/
	pr_info("map end addr is 0x%x\n",arch_setup_info->map_end_virt_addr);
	pr_info("uart addr is 0x%x\n",arch_setup_info->boot_uart_base_addr);
	pr_info("dtb addr is 0x%x\n",arch_setup_info->boot_dtb_header_base_addr);

	/*parse the dtb*/
	struct fdt_header* dtb_header_ptr = (struct fdt_header*)arch_setup_info->boot_dtb_header_base_addr;
	try_map_dtb(dtb_header_ptr,arch_setup_info);
	
	return 0;
}