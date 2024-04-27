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
	bool find_an_order=false;
	
	if(page_number > (1<<BUDDY_MAXORDER))
		return -ENOMEM;
	/*have we used too many physical memory*/
	/*TODO:if so ,we need to swap the memory*/

	/*calculate the upper 2^n size*/
	for(int order=0;order<=BUDDY_MAXORDER;++order){
		u64 size_in_this_order=(1<<order);
		if(size_in_this_order>=page_number){
			alloc_order=order;
			break;
		}
	}
	tmp_order=alloc_order;
	/*first,try to find an order have at least one node to alloc*/
	while(tmp_order<=BUDDY_MAXORDER)
	{
		struct page_frame* header=&(buddy_pmm.zone[zone_number].avaliable_zone_head[tmp_order]);
		if(frame_list_empty(header)){
			tmp_order++;
		}
		else{
			find_an_order=true;
			break;
		}
	}
	if(!find_an_order)
		return -ENOMEM;
	/*second, if the order is bigger, split it*/
	while(tmp_order>alloc_order)
	{
		struct page_frame* header=&(buddy_pmm.zone[zone_number].avaliable_zone_head[tmp_order]);
		struct page_frame* split_node=(struct page_frame*)KERNEL_PHY_TO_VIRT((u64)header);
		frame_list_del_init(split_node);
		int index=split_node-buddy_pmm.zone[zone_number].zone_head_frame[tmp_order];
	}
	/*third,try to del the node at the head of the alloc order list*/


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
