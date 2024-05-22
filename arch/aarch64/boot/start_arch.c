#include <arch/aarch64/boot/arch_setup.h>
#include <arch/aarch64/power_ctrl.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <modules/log/log.h>
#include <modules/dtb/dtb.h>
#include <common/mm.h>
#include <common/endianness.h>
#include <shampoos/error.h>
extern	u64	L2_table;
static void map_dtb(struct setup_info* arch_setup_info)
{
	/*
		map the dtb, using the linux boot protocol, which define that:
		the dtb must be 8 byte align, and less then 2m
		as it haven't defined that dtb must 2m align, we must alloc 4m
	*/
	u64	vaddr=ROUND_UP(arch_setup_info->map_end_virt_addr,MIDDLE_PAGE_SIZE);
	u64	paddr=ROUND_DOWN(arch_setup_info->dtb_ptr,MIDDLE_PAGE_SIZE);
	arch_set_L2_entry_huge(paddr,vaddr,&L2_table,(PT_DESC_V|PT_DESC_ATTR_LOWER_AF));
	vaddr+=MIDDLE_PAGE_SIZE;
	paddr+=MIDDLE_PAGE_SIZE;
	arch_set_L2_entry_huge(paddr,vaddr,&L2_table,(PT_DESC_V|PT_DESC_ATTR_LOWER_AF));
	u64	offset=vaddr-paddr;
	arch_setup_info->boot_dtb_header_base_addr=arch_setup_info->dtb_ptr+offset;
	arch_setup_info->map_end_virt_addr=arch_setup_info->boot_dtb_header_base_addr+MIDDLE_PAGE_SIZE*2;
}
int	start_arch (struct setup_info* arch_setup_info)
{
	/*parse the dtb*/
	map_dtb(arch_setup_info);
	pr_info("dtb addr is 0x%x\n",arch_setup_info->boot_dtb_header_base_addr);
	pr_info("dtb phy addr is 0x%x\n",arch_setup_info->dtb_ptr);
	/*we need to map the dtb page to a virt address first*/
	pr_info("map end addr is 0x%x\n",arch_setup_info->map_end_virt_addr);
	pr_info("uart addr is 0x%x\n",arch_setup_info->boot_uart_base_addr);
	struct fdt_header* dtb_header_ptr = (struct fdt_header*)(arch_setup_info->boot_dtb_header_base_addr);
	if(fdt_check_header(dtb_header_ptr))
	{
		pr_info("check fdt header fail\n");
		goto start_arch_error;
	}
	/*Hint:dtb header is big-endian*/
	pr_info("dtb length is 0x%x\n",SWAP_ENDIANNESS_32(dtb_header_ptr->totalsize));
	pr_info("dtb magic is 0x%x\n",SWAP_ENDIANNESS_32(dtb_header_ptr->magic));
	pr_info("dtb dt struct off 0x%x\n",SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct));
	pr_info("dtb dt string off 0x%x\n",SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_strings));
	pr_info("dtb reserved memory off 0x%x\n",SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap));
	for(u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap);
		off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct);
		off += sizeof(struct fdt_reserve_entry) )
	{
		struct fdt_reserve_entry* entry = (struct fdt_reserve_entry*)((u64)dtb_header_ptr + off);
		pr_info("reserve_entry: address 0x%x size: 0x%x\n",entry->address,entry->size);
	}
	int node_offset=0;
	int depth=0;
	while(node_offset>=0)
	{
		pr_info("offset is 0x%x\n",node_offset);
		char* ch=(char*)dtb_header_ptr + fdt_off_dt_struct(dtb_header_ptr) + node_offset + FDT_TAGSIZE;
		pr_info("%s\n",ch);
		

		node_offset=fdt_next_node((void*)dtb_header_ptr,node_offset,&depth);
	}
	
	return 0;
start_arch_error:
	return -EPERM;
}