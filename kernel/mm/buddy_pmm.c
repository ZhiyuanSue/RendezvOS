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
	struct page_frame *child_order_header=NULL,*left_child=NULL,*right_child=NULL;
	int index=0;
	
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
	pr_info("alloc order is %d\n",alloc_order);
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
		header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order);
		if(frame_list_empty(header)){
			return -ENOMEM;
		}
		del_node=(struct page_frame*)KERNEL_PHY_TO_VIRT((u64)(avaliable_header->next));
		
		index=del_node-header;
		child_order_header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order-1);
		left_child=&child_order_header[index<<1];
		right_child=&child_order_header[(index<<1)+1];

		if(!(left_child->flags & PAGE_FRAME_ALLOCED) || !(right_child->flags & PAGE_FRAME_ALLOCED) )
			return -ENOMEM;

		frame_list_del_init(del_node);
		del_node->flags |= PAGE_FRAME_ALLOCED;
		frame_list_add_head(left_child,child_order_header);
		frame_list_add_head(right_child,child_order_header);

		tmp_order-=1;
	}
	/*third,try to del the node at the head of the alloc order list*/
	avaliable_header=(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number,tmp_order);
	header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order);
	if(frame_list_empty(header)){
		return -ENOMEM;
	}
	del_node=(struct page_frame*)KERNEL_PHY_TO_VIRT((u64)(avaliable_header->next));
	index=del_node-header;
	pr_info("index is %d\n",index);
	frame_list_del_init(del_node);
	del_node->flags |= PAGE_FRAME_ALLOCED;

	while(tmp_order>=0)
	{
		child_order_header=(struct page_frame *)GET_HEAD_PTR(zone_number,tmp_order-1);
		left_child=&child_order_header[index<<1];
		right_child=&child_order_header[(index<<1)+1];
		if(!(left_child->flags & PAGE_FRAME_ALLOCED) || !(right_child->flags & PAGE_FRAME_ALLOCED) )
		{
			pr_info("inner error 1\n");
			return -ENOMEM;
		}
		left_child->flags |= PAGE_FRAME_ALLOCED;
		right_child->flags |= PAGE_FRAME_ALLOCED;
		tmp_order--;
	}
	pr_info("alloc order is %d\n",alloc_order);
	return PPN_FROM_IDX(alloc_order,index);
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
