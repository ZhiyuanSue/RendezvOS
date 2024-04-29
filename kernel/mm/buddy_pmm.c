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
	int	alloc_order=0, tmp_order;
	bool find_an_order=false;
	struct page_frame *avaliable_header=NULL,*header=NULL,*del_node=NULL;
	struct page_frame *child_order_avaliable_header=NULL,*child_order_header=NULL,*left_child=NULL,*right_child=NULL;
	u64 index=0;
	
	if(page_number > (1<<BUDDY_MAXORDER))
		return -ENOMEM;
	/*have we used too many physical memory*/
	if(buddy_pmm.zone[zone_number].zone_total_avaliable_pages<=0)
	{
		pr_error("this zone have no memory to alloc\n");
		/*TODO:if so ,we need to swap the memory*/
		return -ENOMEM;
	}

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
		struct page_frame* header=GET_AVALI_HEAD_PTR(zone_number,tmp_order);
		if(frame_list_empty(header)){
			tmp_order++;
		}
		else{
			find_an_order=true;
			break;
		}
	}
	if(!find_an_order)
	{
		pr_info("not find an order\n");
		return -ENOMEM;
	}
	/*second, if the order is bigger, split it*/
	/*
	* in step 1 ,the alloc_order must >= 0, and if tmp_order == 0, cannot run into while
	* so tmp_order-1 >= 0, and it have a child list
	*/
	while(tmp_order>alloc_order)
	{
		avaliable_header=(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number,tmp_order);
		header=GET_HEAD_PTR(zone_number,tmp_order);
		if(frame_list_empty(avaliable_header))
			return -ENOMEM;
		del_node=(struct page_frame*)KERNEL_PHY_TO_VIRT((u64)(avaliable_header->next));
		index=((u64)del_node-(u64)header)/sizeof(struct page_frame);
		child_order_avaliable_header=(struct page_frame*)GET_AVALI_HEAD_PTR(zone_number,tmp_order-1);
		child_order_header=GET_HEAD_PTR(zone_number,tmp_order-1);
		left_child=&child_order_header[index<<1];
		right_child=&child_order_header[(index<<1)+1];

		if((left_child->flags & PAGE_FRAME_ALLOCED) || \
				(right_child->flags & PAGE_FRAME_ALLOCED) )
			return -ENOMEM;

		frame_list_del_init(del_node);
		del_node->flags |= PAGE_FRAME_ALLOCED;
		frame_list_add_head(left_child,child_order_avaliable_header);
		frame_list_add_head(right_child,child_order_avaliable_header);

		tmp_order-=1;
	}
	/*third,try to del the node at the head of the alloc order list*/
	avaliable_header=(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number,tmp_order);
	header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order);
	if(frame_list_empty(header))
		return -ENOMEM;

	del_node=(struct page_frame*)KERNEL_PHY_TO_VIRT((u64)(avaliable_header->next));
	index=((u64)del_node-(u64)header)/sizeof(struct page_frame);
	frame_list_del_init(del_node);
	del_node->flags |= PAGE_FRAME_ALLOCED;
	while(tmp_order>0)
	{
		child_order_header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order-1);
		left_child=&child_order_header[index<<1];
		right_child=&child_order_header[(index<<1)+1];
		if((left_child->flags & PAGE_FRAME_ALLOCED) || \
				(right_child->flags & PAGE_FRAME_ALLOCED) )
			return -ENOMEM;

		left_child->flags |= PAGE_FRAME_ALLOCED;
		right_child->flags |= PAGE_FRAME_ALLOCED;
		tmp_order--;
	}
	buddy_pmm.zone[zone_number].zone_total_avaliable_pages-=1<<alloc_order;
	pr_debug("we alloced %x pages and have 0x%x pages after alloc\n",1<<alloc_order,buddy_pmm.zone[zone_number].zone_total_avaliable_pages);
	return PPN_FROM_IDX(alloc_order,index);
}
u32	pmm_alloc(size_t page_number)
{
	return pmm_alloc_zone(page_number,ZONE_NORMAL);
}
static bool inline ppn_inrange(u32 ppn)
{
	struct buddy_zone mem_zone;
	bool ppn_inrange=false;
	for(int zone_n=0;zone_n<ZONE_NR_MAX;++zone_n)
	{
		mem_zone=buddy_pmm.zone[zone_n];
		if( PPN(mem_zone.zone_lower_addr)<=ppn &&	\
			PPN(mem_zone.zone_upper_addr)> ppn)
			ppn_inrange=true;
	}
	return ppn_inrange;
}
int pmm_free_one(u32 ppn)
{
	u64 index,buddy_index;
	int tmp_order=0,zone_number=0;
	struct page_frame *avaliable_header=NULL, *header=NULL,*insert_node=NULL;
	struct page_frame *buddy_node=NULL;
	struct buddy_zone mem_zone;
	
	/*judge whether this ppn is in the range of this zone*/
	if(ppn_inrange(ppn)==false)
	{
		pr_error("this ppn is illegal\n");
		return -ENOMEM;
	}
	/*try to insert the node and try to merge*/

	while(tmp_order<=BUDDY_MAXORDER)
	{
		index=IDX_FROM_PPN(tmp_order,ppn);
		avaliable_header=(struct page_frame*)GET_AVALI_HEAD_PTR(zone_number,tmp_order);
		header=GET_HEAD_PTR(zone_number,tmp_order);
		buddy_index=(index>>1)<<1;
		buddy_index=(buddy_index==index)? \
			(buddy_index+1)	:	\
			(buddy_index);
		buddy_node=&(header[buddy_index]);
		insert_node=&(header[index]);
		insert_node->flags &= ~PAGE_FRAME_ALLOCED;
		/*if buddy is not empty ,stop merge,and insert current node into the avaliable list*/
		if(buddy_node->flags & PAGE_FRAME_ALLOCED){
			frame_list_add_head(insert_node,avaliable_header);
			break;
		}
		/*else try merge*/
		frame_list_del_init(buddy_node);
		tmp_order++;
	}
	mem_zone=buddy_pmm.zone[zone_number];
	mem_zone.zone_total_avaliable_pages++;
	return 0;
}
int pmm_free(u32 ppn,size_t page_number)
{
	bool ppn_inrange=false;
	int alloc_order=0;
	int free_one_result=0;
	if(ppn==-ENOMEM)
		return -ENOMEM;
	for(int order=0;order<=BUDDY_MAXORDER;++order){
		u64 size_in_this_order=(1<<order);
		if(size_in_this_order>=page_number){
			alloc_order=order;
			break;
		}
	}
	for(int page_count=0;page_count<(1<<alloc_order);page_count++){
		if(!(free_one_result=pmm_free_one(ppn+page_count)))
			return free_one_result;
	}
	return 0;
}
struct	pmm	buddy_pmm = {
	.pmm_init=pmm_init,
	.pmm_alloc=pmm_alloc,
	.pmm_free_one=pmm_free_one,
	.pmm_free=pmm_free
};
