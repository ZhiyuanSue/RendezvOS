#include <shampoos/mm/pmm.h>
#include <arch/x86_64/mm/page_table.h>
#include <modules/log/log.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/power_ctrl.h>
extern char _end;	/*the kernel end virt addr*/
extern pt_entry boot_page_table_PML4,boot_page_table_directory_ptr,boot_page_table_directory;
extern struct pmm buddy_pmm;

static	u64	entry_per_bucket[BUDDY_MAXORDER], pages_per_bucket[BUDDY_MAXORDER];
static	void	calculate_bucket_space(u64	adjusted_phy_mem_end)
{
	/*we promised that this phy mem end is 2m aligned*/
	for(int order=0;order<BUDDY_MAXORDER;++order)
	{
		u64 size_in_this_order=(PAGE_SIZE<<order);
		entry_per_bucket[order]=adjusted_phy_mem_end/size_in_this_order;
		pages_per_bucket[order]=ROUND_UP(entry_per_bucket[order],PAGE_SIZE);
	}
}
static	void	try_map_buddy_data_space(void)
{

}
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* mtb_info = \
		GET_MULTIBOOT_INFO(arch_setup_info);
	struct multiboot_mmap_entry* mmap;
	u64 add_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
	u64 length = mtb_info->mmap.mmap_length;

	if( !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MEM) || \
	 !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MMAP))
	{
		pr_info("no mem info\n");
		goto arch_init_pmm_error;
	}
	buddy_pmm.avaliable_phy_addr_end = ROUND_DOWN(((u64)(mtb_info->mem.mem_upper)) << 10 , MIDDLE_PAGE_SIZE);
	for(int order=0;order<BUDDY_MAXORDER;++order)
		entry_per_bucket[order]=pages_per_bucket[order]=0;

	/*calculate how mach the buddy data need*/
	calculate_bucket_space(buddy_pmm.avaliable_phy_addr_end);

	/*check whether the kernel can put in all*/

	/*try to find an avaliable region to place the buddy data*/

	try_map_buddy_data_space();
	// for(mmap = (struct multiboot_mmap_entry*)add_ptr;
	// 	((u64)mmap) < (add_ptr+length);
	// 	mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	// {
	// 	/*pr_info("Aviable Mem:size is 0x%x, base_phy_addr is 0x%x, length = 0x%x, type = 0x%x\n"	\
	// 			,mmap->size,mmap->addr,mmap->len,mmap->type);*/
	// 	if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
	// 	{
	// 		u64 sec_start_addr=mmap->addr;
	// 		u64 sec_end_addr=sec_start_addr+mmap->len;
	// 				/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr ) */
	// 		if(check_region(&sec_start_addr,&sec_end_addr))
	// 			can_load_kernel=true;
	// 		total_avaliable_memory+=sec_end_addr-sec_start_addr;
	// 		/*calculate the buddy entries per bucket*/
	// 		for(int order=0;order<BUDDY_MAXORDER;++order)
	// 		{
	// 			u64 size_in_this_order=(PAGE_SIZE<<order);
	// 			/*this calculate is the maximal one*/
	// 			buddy_entries_per_bucket[zone_number][order]+= \
	// 				( ROUND_DOWN(sec_end_addr,size_in_this_order) \
	// 				-ROUND_UP(sec_start_addr,size_in_this_order) )/size_in_this_order;
	// 		}
	// 	}
	// }
	// /*You need to check whether the kernel have been loaded all successfully*/
	// if(!can_load_kernel)
	// {
	// 	pr_info("cannot load kernel\n");
	// 	goto arch_init_pmm_error;
	// }
	// /*calculate and static alloc the pmm data struct 
	// * remember,we must find an avaliable phy region that is large enough to place those management data
	// * if we cannot find one , another error
	// */
	// pr_info("total avaliable memory are 0x%x\n",total_avaliable_memory);
	// for(int zone=0;zone<ZONE_NR_MAX;++zone)
	// {
	// 	for(int order=0;order<BUDDY_MAXORDER;++order)
	// 	{
	// 		u64 entries_total_size	=	buddy_entries_per_bucket[zone][order]*sizeof(struct page_frame);
	// 		pr_info("buddy entries in bucket %d total 0x%x entries, need 0x%x space\n", \
	// 			order,buddy_entries_per_bucket[order],entries_total_size);
	// 		buddy_pages_per_bucket[zone][order]	=	ROUND_UP(entries_total_size,PAGE_SIZE)/PAGE_SIZE;
	// 		buddy_record_pages	+=	buddy_pages_per_bucket[zone][order];
	// 		pr_info("this bucket need buddy record pages is 0x%x\n", \
	// 			buddy_pages_per_bucket[zone][order]);
	// 	}
	// }
	
	// pr_info("total buddy record pages is 0x%x\n",buddy_record_pages);
	// for(mmap = (struct multiboot_mmap_entry*)add_ptr;
	// 	((u64)mmap) < (add_ptr+length);
	// 	mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	// {
	// 	if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
	// 	{
	// 		u64 sec_start_addr = mmap->addr;
	// 		u64 sec_end_addr = sec_start_addr+mmap->len;
	// 		check_region(&sec_start_addr,&sec_end_addr);
	// 		if(sec_end_addr-sec_start_addr > buddy_record_pages* PAGE_SIZE)
	// 		{
	// 			can_record_buddy = true;
	// 			buddy_start_addr = sec_start_addr;
	// 			break;
	// 		}
	// 	}
	// }
	// pr_info("buddy start addr is 0x%x\n",buddy_start_addr);
	// if(!can_record_buddy)
	// {
	// 	pr_info("have no space record buddy\n");
	// 	goto arch_init_pmm_error;
	// }
	// /*finish calculate ,and map those buddy pages using buddy start addr and buddy record pages*/
	// /* calculate the upper alignment of the _end, and judge,
	//  * consider that we might have more then one region,
	//  * so the buddy_start_addr might not next the physical position of kernel_end_phy_addr
	//  */
	// kernel_end_phy_addr_round_up = ROUND_UP(kernel_end_phy_addr,MIDDLE_PAGE_SIZE);
	// buddy_end_addr = buddy_start_addr + buddy_record_pages*PAGE_SIZE;
	// buddy_pmm.kernel_phy_end=buddy_end_addr;
	// buddy_map_start_addr=buddy_start_addr;
	// if(kernel_end_phy_addr_round_up >= buddy_start_addr)
	// 	buddy_map_start_addr = kernel_end_phy_addr_round_up;
	// pr_info("buddy end addr is 0x%x\n",buddy_end_addr);
	// for(; buddy_map_start_addr < buddy_end_addr ; buddy_map_start_addr += MIDDLE_PAGE_SIZE)
	// {
	// 	/*As pmm and vmm part is not usable now, we still use boot page table*/
	// 	pt_entry*	entry	=	&boot_page_table_directory;
	// 	u64	buddy_start_round_down_2m	=	ROUND_DOWN(buddy_map_start_addr,MIDDLE_PAGE_SIZE);
	// 	u32 index	=	PDT(KERNEL_PHY_TO_VIRT(buddy_start_round_down_2m));
	// 	entry[index]	=	PDE_ADDR_2M(buddy_start_round_down_2m,arch_setup_info->phy_addr_width) | PDE_P | PDE_RW | PDE_G | PDE_PS;
	// 	pr_info("buddy_start_round_down_2m 0x%x index 0x%x entry 0x%x\n",buddy_start_round_down_2m,index,entry[index]);
	// }
	// /*generate the buddy bucket*/
	// buddy_bucket_page_ptr=KERNEL_PHY_TO_VIRT(buddy_start_addr);
	// for(int zone=0;zone<ZONE_NR_MAX;++zone)
	// {
	// 	struct	buddy_bucket* tmp_bucket=&buddy_pmm.mem_zone[zone].buckets;
	// 	for(int order=0;order<BUDDY_MAXORDER;++order)
	// 	{
	// 		tmp_bucket[order].zone=&buddy_pmm.mem_zone[zone];
	// 		pr_info("frame addr is %x\n",buddy_bucket_page_ptr);
	// 		tmp_bucket[order].frames=(struct page_frame*)buddy_bucket_page_ptr;
	// 		buddy_bucket_page_ptr+=buddy_pages_per_bucket[zone][order]*PAGE_SIZE;
	// 		tmp_bucket[order].order=order;
	// 		INIT_LIST_HEAD(&tmp_bucket[order].bucket_list_head);
	// 	}
	// }
	// pr_info("end generate buddy bucket\n");
	// /*generate the buddy record*/
	// for(mmap = (struct multiboot_mmap_entry*)add_ptr;
	// 	((u64)mmap) < (add_ptr+length);
	// 	mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	// {
	// 	if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
	// 	{
	// 		u64 sec_start_addr = mmap->addr;
	// 		u64 sec_end_addr = sec_start_addr+mmap->len;
	// 		check_region(&sec_start_addr,&sec_end_addr);
	// 		if(buddy_start_addr = sec_start_addr)
	// 			sec_start_addr=buddy_end_addr;
	// 		zone_number=0;
	// 		u64 align_2m_end=ROUND_DOWN(sec_end_addr,MIDDLE_PAGE_SIZE);
	// 		u64 align_2m_start=ROUND_UP(sec_start_addr,MIDDLE_PAGE_SIZE);
	// 		for(int order=0;order<BUDDY_MAXORDER;++order)
	// 		{
	// 			u64 size_in_this_order=(PAGE_SIZE<<order);
	// 			u64 end_in_this_order=align_2m_end;
	// 			u64 start_in_this_order=align_2m_start;
	// 			struct page_frame* tmp_frame = \
	// 				buddy_pmm.mem_zone[zone_number].buckets[order].frames;
	// 			for(int index=0; \
	// 				start_in_this_order<end_in_this_order; \
	// 				start_in_this_order+=size_in_this_order,index++)
	// 			{
	// 				// pr_info("test 5 %x %x\n",start_in_this_order,index);
	// 				tmp_frame[index].page_address=start_in_this_order;
	// 				tmp_frame[index].flags=PAGE_FRAME_UNALLOCED;
	// 			}
	// 			// pr_info("test 6\n");
	// 		}
	// 	}
	// }
	/*link the list*/

	/*check the buddy data*/

	return;
arch_init_pmm_error:
	arch_shutdown();
}
