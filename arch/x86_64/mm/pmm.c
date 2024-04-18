#include <arch/x86_64/mm/pmm.h>
#ifdef	LOG
#include <modules/log/log.h>
#endif
void arch_init_pmm(struct setup_info* arch_setup_info)
{
	struct multiboot_info* mtb_info = \
		GET_MULTIBOOT_INFO(arch_setup_info);
#ifdef	LOG
	pr_info("mtb info addr is %x\n",(u64)mtb_info);
#endif
	if( MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MEM) )
	{
		pr_info("the mem lower is 0x%x ,the upper is 0x%x\n",mtb_info->mem.mem_lower,mtb_info->mem.mem_upper);
	}else{
		pr_info("no mem info\n");
		return;
	}
	if(MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,MULTIBOOT_INFO_FLAG_MMAP))
	{
		struct multiboot_mmap_entry* mmap;
		u64 add_ptr = mtb_info->mmap.mmap_addr+KERNEL_VIRT_OFFSET;
		u64 length = mtb_info->mmap.mmap_length;
		pr_info("multiboot have mmap with mmap_addr 0x%x map_length 0x%x\n",add_ptr,length);
		mmap = (struct multiboot_mmap_entry*)add_ptr;
		for(mmap = (struct multiboot_mmap_entry*)add_ptr;
			((u64)mmap) < (add_ptr+length);
			mmap = (struct multiboot_mmap_entry*)((u64)mmap + mmap->size + sizeof(mmap->size)))
		{
			if( mmap->addr+mmap->len<=BIOS_MEM_UPPER || mmap->type != 1)
				;
			else{
				pr_info("Aviable Mem:size is 0x%x, base_phy_addr is 0x%x, length = 0x%x, type = 0x%x\n"	\
					,mmap->size,mmap->addr,mmap->len,mmap->type);
			}
			
		}
		/*You need to check whether the kernel have been load all successfully*/
		/*And reserve the memory of the 0-1M*/
	}
	return;
}
