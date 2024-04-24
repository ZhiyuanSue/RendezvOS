#include <shampoos/list.h>
#include <shampoos/mm/pmm.h>
#include <modules/log/log.h>
#include <shampoos/error.h>

struct pmm buddy_pmm;

void pmm_init(struct setup_info* arch_setup_info){
	pr_info("start pmm init\n");
	arch_init_pmm(arch_setup_info);
}
u32 pmm_alloc_zone(size_t page_number,int zone_number)
{
	struct buddy_zone* mem_zone=&buddy_pmm.zone[zone_number];
	int	alloc_order=0, tmp_order;
	
	if(page_number > (1<<BUDDY_MAXORDER))
		return -ENOMEM;
	/*calculate the upper 2^n size*/
	for(int order=0;order<=BUDDY_MAXORDER;++order){
		u64 size_in_this_order=(1<<order);
		if(size_in_this_order>=page_number){
			alloc_order=order;
			break;
		}
	}
	return PPN(buddy_pmm.avaliable_phy_addr_end);
}
u32	pmm_alloc(size_t page_number)
{
	return pmm_alloc_zone(page_number,ZONE_NORMAL);
}
u64 pmm_free_one(u32 ppn)
{

}
u64 pmm_free(u32 ppn,size_t page_number)
{

}
struct	pmm	buddy_pmm = {
	.pmm_init=pmm_init,
	.pmm_alloc=pmm_alloc,
	.pmm_free_one=pmm_free_one,
	.pmm_free=pmm_free
};
