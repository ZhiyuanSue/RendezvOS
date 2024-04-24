#include <shampoos/mm/pmm.h>
#include <arch/x86_64/mm/page_table.h>
#include <modules/log/log.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/power_ctrl.h>
extern char _start,_end;	/*the kernel end virt addr*/
extern pt_entry boot_page_table_PML4,boot_page_table_directory_ptr,boot_page_table_directory;
extern struct pmm buddy_pmm;

static	u64	entry_per_bucket[BUDDY_MAXORDER+1], pages_per_bucket[BUDDY_MAXORDER+1];
static	u64	kernel_phy_start=0,kernel_phy_end=0;
static	u64 buddy_phy_start=0,buddy_phy_end=0;
static	void	calculate_bucket_space(u64	adjusted_phy_mem_end)
{
	/*we promised that this phy mem end is 2m aligned*/
	for(int order=0;order<=BUDDY_MAXORDER;++order)
	{
		u64 size_in_this_order=(PAGE_SIZE<<order);
		entry_per_bucket[order]=adjusted_phy_mem_end/size_in_this_order;
		pages_per_bucket[order]=	\
			ROUND_UP(entry_per_bucket[order]*sizeof(struct page_frame),PAGE_SIZE)/PAGE_SIZE;
	}
}
static	void	try_map_buddy_data_space(u32 m_width)
{
	u64	buddy_phy_start_addr=buddy_phy_start;
	u64	kernel_end_phy_addr_round_up = ROUND_UP(kernel_phy_end,MIDDLE_PAGE_SIZE);
	if(buddy_phy_start_addr<kernel_end_phy_addr_round_up)
		buddy_phy_start_addr=kernel_end_phy_addr_round_up;	/*for we have mapped the 2m align space of kernel*/
	for(; buddy_phy_start_addr < buddy_phy_end ; buddy_phy_start_addr += MIDDLE_PAGE_SIZE)
	{
		/*As pmm and vmm part is not usable now, we still use boot page table*/
		pt_entry*	entry	=	&boot_page_table_directory;
		u64	buddy_start_round_down_2m	=	ROUND_DOWN(buddy_phy_start_addr,MIDDLE_PAGE_SIZE);
		u32 index	=	PDT(KERNEL_PHY_TO_VIRT(buddy_start_round_down_2m));
		entry[index]	=	PDE_ADDR_2M(buddy_start_round_down_2m,m_width) | PDE_P | PDE_RW | PDE_G | PDE_PS;
		pr_info("buddy map 2m pages 0x%x index 0x%x entry 0x%x\n",buddy_start_round_down_2m,index,entry[index]);
	}
}
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* mtb_info = \
		GET_MULTIBOOT_INFO(arch_setup_info);
	struct multiboot_mmap_entry* mmap;
	u64 add_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
	u64 length = mtb_info->mmap.mmap_length;
	u64 buddy_total_pages=0;

	bool can_load_kernel=false;

	if( !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MEM) || \
	 !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MMAP))
	{
		pr_info("no mem info\n");
		goto arch_init_pmm_error;
	}
	kernel_phy_start=KERNEL_VIRT_TO_PHY((u64)(&_start));
	kernel_phy_end=KERNEL_VIRT_TO_PHY((u64)(&_end));
	buddy_phy_start=ROUND_UP(kernel_phy_end,PAGE_SIZE);

	buddy_pmm.avaliable_phy_addr_end = 0;
	
	for(mmap = (struct multiboot_mmap_entry*)add_ptr;
		((u64)mmap) < (add_ptr+length);
		mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	{
		if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
		{
			pr_info("Aviable Mem:size is 0x%x, base_phy_addr is 0x%x, length = 0x%x, type = 0x%x\n"	\
				,mmap->size,mmap->addr,mmap->len,mmap->type);
			u64 sec_start_addr=mmap->addr;
			u64 sec_end_addr=sec_start_addr+mmap->len;
					/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr ) */
			if(sec_end_addr>buddy_pmm.avaliable_phy_addr_end)
				buddy_pmm.avaliable_phy_addr_end=sec_end_addr;
		}
	}
	
	for(int order=0;order<=BUDDY_MAXORDER;++order)
		entry_per_bucket[order]=pages_per_bucket[order]=0;
	/*calculate how mach the buddy data need*/
	calculate_bucket_space(buddy_pmm.avaliable_phy_addr_end);
	for(int order=0;order<=BUDDY_MAXORDER;++order)
		buddy_total_pages += pages_per_bucket[order];
	buddy_phy_end = buddy_phy_start + buddy_total_pages * PAGE_SIZE;
	pr_info("buddy start 0x%x end 0x%x\n",buddy_phy_start,buddy_phy_end);
	/*check whether the kernel can put in all*/
	/*try to check an avaliable region to place the kernel and buddy data*/

	for(mmap = (struct multiboot_mmap_entry*)add_ptr;
		((u64)mmap) < (add_ptr+length);
		mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	{
		if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
		{
			u64 sec_start_addr=mmap->addr;
			u64 sec_end_addr=sec_start_addr+mmap->len;
					/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr ) */
			if(sec_start_addr<=kernel_phy_start && sec_end_addr >= buddy_phy_end)
				can_load_kernel=true;
		}
	}
	/*You need to check whether the kernel have been loaded all successfully*/
	if(!can_load_kernel)
	{
		pr_info("cannot load kernel\n");
		goto arch_init_pmm_error;
	}
	
	try_map_buddy_data_space(arch_setup_info->phy_addr_width);

	/*generate the buddy bucket*/
	for(int order=0;order<=BUDDY_MAXORDER;++order)
	{
		buddy_pmm.buckets[order].order=order;
		buddy_pmm.buckets[order].pages=(struct page_frame*)KERNEL_PHY_TO_VIRT(buddy_phy_start);
		buddy_phy_start+=pages_per_bucket[order];
	}
	for(mmap = (struct multiboot_mmap_entry*)add_ptr;
		((u64)mmap) < (add_ptr+length);
		mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
	{
		if( mmap->addr+mmap->len>BIOS_MEM_UPPER && mmap->type == MULTIBOOT_MEMORY_AVAILABLE )
		{
			u64 sec_start_addr=mmap->addr;
			u64 sec_end_addr=sec_start_addr+mmap->len;
					/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr ) */
			if(sec_start_addr<=kernel_phy_start && sec_end_addr >= buddy_phy_end)
				sec_start_addr=buddy_phy_end;
			sec_start_addr=ROUND_UP(sec_start_addr,MIDDLE_PAGE_SIZE);
			sec_end_addr=ROUND_DOWN(sec_end_addr,MIDDLE_PAGE_SIZE);
			for(int order=0;order<=BUDDY_MAXORDER;++order)
			{
				u64 size_in_this_order=(PAGE_SIZE<<order);
				struct page_frame* pages=buddy_pmm.buckets[order].pages;
				for(u64 addr_iter=sec_start_addr;addr_iter<sec_end_addr;addr_iter+=size_in_this_order)
				{
					u32 index=IDX_FROM_PPN(order,PPN(addr_iter));
					pages[index].flags |= PAGE_FRAME_AVALIABLE;
					pages[index].prev=pages[index].next=KERNEL_VIRT_TO_PHY((u64)&(pages[index]));
				}
			}
		}
	}
	/*init the zones,remember there might have more then one zone*/
	for(int mem_zone=0;mem_zone<ZONE_NR_MAX;++mem_zone)
	{
		struct buddy_zone* zone=&buddy_pmm.zone[mem_zone];
		switch (mem_zone)
		{
		case ZONE_NORMAL:
			zone->zone_lower_addr=0;
			zone->zone_upper_addr=buddy_pmm.avaliable_phy_addr_end;
			break;
		default:
			break;
		}
		for(int order=0;order<=BUDDY_MAXORDER;++order)
		{
			zone->zone_head_frame[order]=	\
				&buddy_pmm.buckets[order].pages[IDX_FROM_PPN(order,PPN(zone->zone_lower_addr))];
			zone->avaliable_zone_head[order].prev=KERNEL_VIRT_TO_PHY((u64)&(zone->avaliable_zone_head[order]));
			zone->avaliable_zone_head[order].next=KERNEL_VIRT_TO_PHY((u64)&(zone->avaliable_zone_head[order]));
		}
	}
	/*link the list*/
	for(u64 addr_iter=0;	\
		addr_iter<buddy_pmm.avaliable_phy_addr_end;	\
		addr_iter+=(PAGE_SIZE<<BUDDY_MAXORDER))
	{
		u32 index=IDX_FROM_PPN(BUDDY_MAXORDER,PPN(addr_iter));
		struct page_frame* page=&buddy_pmm.buckets[BUDDY_MAXORDER].pages[index];
		if( page->flags & PAGE_FRAME_AVALIABLE)
		{
			if(	addr_iter >= buddy_pmm.zone[ZONE_NORMAL].zone_lower_addr &&
				addr_iter < buddy_pmm.zone[ZONE_NORMAL].zone_upper_addr)
			{	/*zone normal*/
				frame_list_add_head(page,&buddy_pmm.zone[ZONE_NORMAL].avaliable_zone_head[BUDDY_MAXORDER]);	
			}
		}
	}
	
	/*check the buddy data*/

	return;
arch_init_pmm_error:
	arch_shutdown();
}
