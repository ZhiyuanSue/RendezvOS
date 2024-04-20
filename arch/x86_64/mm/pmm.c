#include <arch/x86_64/mm/pmm.h>
#include <modules/log/log.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/power_ctrl.h>
extern char _end;	/*the kernel end virt addr*/
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* mtb_info = \
		GET_MULTIBOOT_INFO(arch_setup_info);
	pr_info("mtb info addr is %x\n",(u64)mtb_info);
	if( MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MEM) )
	{
		pr_info("the mem lower is 0x%x ,the upper is 0x%x\n",mtb_info->mem.mem_lower,mtb_info->mem.mem_upper);
	}else{
		pr_info("no mem info\n");
		arch_shutdown();
	}
	if(MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MMAP))
	{
		struct multiboot_mmap_entry* mmap;
		u64	kernel_end_phy_addr=(u64)(&_end) & KERNEL_VIRT_OFFSET_MASK;
		u64 add_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
		u64 length = mtb_info->mmap.mmap_length;
		pr_info("multiboot have mmap with mmap_addr 0x%x map_length 0x%x\n",add_ptr,length);
		mmap = (struct multiboot_mmap_entry*)add_ptr;
		for(mmap = (struct multiboot_mmap_entry*)add_ptr;
			((u64)mmap) < (add_ptr+length);
			mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
		{
			if( mmap->addr+mmap->len<=BIOS_MEM_UPPER || mmap->type != MULTIBOOT_MEMORY_AVAILABLE )
				;
			else{
				pr_info("Aviable Mem:size is 0x%x, base_phy_addr is 0x%x, length = 0x%x, type = 0x%x\n"	\
					,mmap->size,mmap->addr,mmap->len,mmap->type);
				u64 sec_start_addr=mmap->addr;
				u64 sec_end_addr=sec_start_addr+mmap->len;
						/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr ) */
				u64 usable_start_addr=0;
				u64 usable_end_addr=0;
				pr_info("start 0x%x,end 0x%x\n",sec_start_addr,sec_end_addr);
				/*
				 * the physical memory layout now is 
				 * 0 - BIOS_MEM_UPPER, bios part and we don't use it.
				 * BIOS_MEM_UPPE - _end,kernel part
				 * */
				if(sec_start_addr <= BIOS_MEM_UPPER)
				{
					if(sec_end_addr <= BIOS_MEM_UPPER ){
						;/*do nothing*/
					}
					else{
						if(sec_end_addr < kernel_end_phy_addr)
						{
							pr_info("the kernel cannot load all, exit\n");
							arch_shutdown();
						}
						else{
							usable_start_addr = kernel_end_phy_addr;
							usable_end_addr = sec_end_addr;
						}
					}
				}
				else	/*the end must >= BIOS_MEM_UPPER */
				{
					pr_info("the kernel cannot load all,exit\n");
					arch_shutdown();
				}
				if(usable_start_addr && usable_end_addr)
				{
					pr_info("usable start addr 0x%x,usable end addr 0x%x\n",usable_start_addr,usable_end_addr);
				}
				else{
					pr_info("this section not usable\n");
				}



			}
		}
		/*You need to check whether the kernel have been load all successfully*/
		/*And reserve the memory of the 0-1M*/
	}
	return;
}
