#include <modules/log/log.h>
#include <shampoos/error.h>
#include <shampoos/mm/buddy_pmm.h>

struct buddy buddy_pmm;
extern struct memory_regions m_regions;
u64 entry_per_bucket[BUDDY_MAXORDER + 1], pages_per_bucket[BUDDY_MAXORDER + 1];
/*some public functions in arch init, you can see some example in arch pmm*/
void calculate_bucket_space() {
	paddr adjusted_phy_mem_end = buddy_pmm.avaliable_phy_addr_end;
	/*we promised that this phy mem end is 2m aligned*/
	for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
		u64 size_in_this_order = (PAGE_SIZE << order);
		entry_per_bucket[order] = adjusted_phy_mem_end / size_in_this_order;
		pages_per_bucket[order] =
			ROUND_UP(entry_per_bucket[order] * sizeof(struct page_frame),
					 PAGE_SIZE) /
			PAGE_SIZE;
	}
}
void calculate_avaliable_phy_addr_end() {
	buddy_pmm.avaliable_phy_addr_end = 0;
	for (int i = 0; i < buddy_pmm.m_regions->region_count; i++) {
		if (buddy_pmm.m_regions->memory_regions_entry_empty(i))
			continue;
		struct region reg = buddy_pmm.m_regions->memory_regions[i];
		pr_info(
			"Aviable Mem:base_phy_addr is 0x%x, length = "
			"0x%x\n",
			reg.addr, reg.len);
		paddr sec_start_addr = reg.addr;
		paddr sec_end_addr = sec_start_addr + reg.len;
		/*remember, this end is not reachable,[ sec_end_addr , sec_end_addr
		 * ) */
		if (sec_end_addr > buddy_pmm.avaliable_phy_addr_end)
			buddy_pmm.avaliable_phy_addr_end = sec_end_addr;
	}
	buddy_pmm.avaliable_phy_addr_end =
		ROUND_DOWN(buddy_pmm.avaliable_phy_addr_end, MIDDLE_PAGE_SIZE);
}
void generate_buddy_bucket(paddr kernel_phy_start, paddr kernel_phy_end,
						   paddr buddy_phy_start, paddr buddy_phy_end) {
	/*generate the buddy bucket*/
	for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
		buddy_pmm.buckets[order].order = order;
		buddy_pmm.buckets[order].pages =
			(struct page_frame *)KERNEL_PHY_TO_VIRT(buddy_phy_start);
		buddy_phy_start += pages_per_bucket[order] * PAGE_SIZE;
		// TODO: 每个链表的page
		// frame头都需要按照实际分配而不能连续分配，需要重新计算过。
	}
	for (int i = 0; i < buddy_pmm.m_regions->region_count; i++) {
		if (buddy_pmm.m_regions->memory_regions_entry_empty(i))
			continue;
		struct region reg = buddy_pmm.m_regions->memory_regions[i];
		paddr sec_start_addr = reg.addr;
		paddr sec_end_addr = sec_start_addr + reg.len;
		/*this end is not reachable,[ sec_end_addr , sec_end_addr) */
		if (sec_start_addr <= kernel_phy_start && sec_end_addr >= buddy_phy_end)
			sec_start_addr = buddy_phy_end;
		// TODO：同样的，因为每个链表的page frame
		// array可以移动，所以后续fill的时候需要把这一部分的填充也考虑进去。
		// 不能简单的这么计算，而是可能需要考虑到section的分裂。
		sec_start_addr = ROUND_UP(sec_start_addr, MIDDLE_PAGE_SIZE);
		sec_end_addr = ROUND_DOWN(sec_end_addr, MIDDLE_PAGE_SIZE);
		for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
			u64 size_in_this_order = (PAGE_SIZE << order);
			struct page_frame *pages = buddy_pmm.buckets[order].pages;
			for (paddr addr_iter = sec_start_addr; addr_iter < sec_end_addr;
				 addr_iter += size_in_this_order) {
				u32 index = IDX_FROM_PPN(order, PPN(addr_iter));
				pages[index].flags |= PAGE_FRAME_AVALIABLE;
				pages[index].prev = pages[index].next = index;
			}
		}
	}
	return;
}

static void pmm_init_zones() {
	/*init the zones,remember there might have more then one zone*/
	for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
		struct buddy_zone *zone = &(buddy_pmm.zone[mem_zone]);
		switch (mem_zone) {
		case ZONE_NORMAL:
			zone->zone_lower_addr = 0;
			zone->zone_upper_addr = buddy_pmm.avaliable_phy_addr_end;
			pr_info("avaliable phy addr end 0x%x\n", zone->zone_upper_addr);
			break;
		default:
			break;
		}
		for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
			zone->zone_head_frame[order] =
				&(buddy_pmm.buckets[order]
					  .pages[IDX_FROM_PPN(order, PPN(zone->zone_lower_addr))]);
			zone->avaliable_frame[order] = NULL;
			zone->zone_total_pages = zone->zone_total_avaliable_pages = 0;
		}
		for (paddr addr_iter = 0; addr_iter < buddy_pmm.avaliable_phy_addr_end;
			 addr_iter += (PAGE_SIZE << BUDDY_MAXORDER)) {
			u32 index = IDX_FROM_PPN(BUDDY_MAXORDER, PPN(addr_iter));
			struct page_frame *page =
				&(buddy_pmm.buckets[BUDDY_MAXORDER].pages[index]);
			if (page->flags & PAGE_FRAME_AVALIABLE) {
				if (addr_iter >= buddy_pmm.zone[ZONE_NORMAL].zone_lower_addr &&
					addr_iter < buddy_pmm.zone[ZONE_NORMAL].zone_upper_addr) {
					buddy_pmm.zone[ZONE_NORMAL].zone_total_avaliable_pages +=
						1 << BUDDY_MAXORDER;
				}
			}
			buddy_pmm.zone[ZONE_NORMAL].zone_total_pages += 1 << BUDDY_MAXORDER;
		}
	}
	/*link the list*/
	for (paddr addr_iter = 0; addr_iter < buddy_pmm.avaliable_phy_addr_end;
		 addr_iter += (PAGE_SIZE << BUDDY_MAXORDER)) {
		u32 index = IDX_FROM_PPN(BUDDY_MAXORDER, PPN(addr_iter));
		struct page_frame *page =
			&(buddy_pmm.buckets[BUDDY_MAXORDER].pages[index]);
		if (page->flags & PAGE_FRAME_AVALIABLE) {
			if (addr_iter >= buddy_pmm.zone[ZONE_NORMAL].zone_lower_addr &&
				addr_iter < buddy_pmm.zone[ZONE_NORMAL]
								.zone_upper_addr) { /*zone normal*/
				if (!buddy_pmm.zone[ZONE_NORMAL]
						 .avaliable_frame[BUDDY_MAXORDER]) {
					buddy_pmm.zone[ZONE_NORMAL]
						.avaliable_frame[BUDDY_MAXORDER] = page;
				} else {
					frame_list_add_head(buddy_pmm.buckets[BUDDY_MAXORDER].pages,
										page,
										buddy_pmm.zone[ZONE_NORMAL]
											.avaliable_frame[BUDDY_MAXORDER]);
				}
			}
		}
	}

	/*check the buddy data*/

	return;
}

void pmm_init(struct setup_info *arch_setup_info) {
	pr_info("start pmm init\n");
	arch_init_pmm(arch_setup_info);
	pmm_init_zones();
}
static inline int calculate_alloc_order(size_t page_number) {
	int alloc_order = 0;
	for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
		u64 size_in_this_order = (1 << order);
		if (size_in_this_order >= page_number) {
			alloc_order = order;
			break;
		}
	}
	return alloc_order;
}
static inline u32 mark_childs(int zone_number, int order, u64 index) {
	struct page_frame *del_node = &(GET_HEAD_PTR(zone_number, order)[index]);
	if (order < 0)
		return 0;
	if (del_node->flags & PAGE_FRAME_ALLOCED)
		return -ENOMEM;
	del_node->flags |= PAGE_FRAME_ALLOCED;
	if (mark_childs(zone_number, order - 1, index << 1) ||
		mark_childs(zone_number, order - 1, (index << 1) + 1))
		return -ENOMEM;
	return 0;
}
u32 pmm_alloc_zone(size_t page_number, int zone_number) {
	pr_info("go into pmm alloc zone\n");
	int alloc_order = 0, tmp_order;
	bool find_an_order = false;
	struct page_frame *avaliable_header = NULL, *header = NULL,
					  *del_node = NULL;
	struct page_frame *child_order_avaliable_header = NULL,
					  *child_order_header = NULL, *left_child = NULL,
					  *right_child = NULL;
	u64 index = 0;

	if (page_number > (1 << BUDDY_MAXORDER))
		return -ENOMEM;

	/*calculate the upper 2^n size*/
	alloc_order = calculate_alloc_order(page_number);
	tmp_order = alloc_order;
	/*first,try to find an order have at least one node to alloc*/
	for (; tmp_order <= BUDDY_MAXORDER; tmp_order++) {
		struct page_frame *header = GET_AVALI_HEAD_PTR(zone_number, tmp_order);
		if (!header) {
			find_an_order = true;
			break;
		}
	}
	if (!find_an_order) {
		pr_info("not find an order\n");
		return -ENOMEM;
	}
	/*second, if the order is bigger, split it*/
	/*
	 * in step 1 ,the alloc_order must >= 0, and if tmp_order == 0, cannot run
	 * into while so tmp_order-1 >= 0, and it have a child list
	 */
	pr_info("start while\n");
	while (tmp_order > alloc_order) {
		pr_info("in while 0\n");
		avaliable_header =
			(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number, tmp_order);
		header = GET_HEAD_PTR(zone_number, tmp_order);
		if (!avaliable_header)
			return -ENOMEM;
		del_node = avaliable_header;
		index = del_node - header;
		child_order_avaliable_header =
			(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number, tmp_order - 1);
		child_order_header = GET_HEAD_PTR(zone_number, tmp_order - 1);
		left_child = &child_order_header[index << 1];
		right_child = &child_order_header[(index << 1) + 1];

		if ((left_child->flags & PAGE_FRAME_ALLOCED) ||
			(right_child->flags & PAGE_FRAME_ALLOCED))
			return -ENOMEM;

		frame_list_del_init(buddy_pmm.buckets[tmp_order].pages, del_node);
		del_node->flags |= PAGE_FRAME_ALLOCED;
		if (frame_list_only_one(buddy_pmm.buckets[tmp_order].pages, del_node))
			buddy_pmm.zone[zone_number].avaliable_frame[tmp_order] = NULL;
		if (!child_order_avaliable_header) {
			child_order_avaliable_header = left_child;
		} else {
			frame_list_add_head(buddy_pmm.buckets[tmp_order].pages, left_child,
								child_order_avaliable_header);
		}
		// after this, the child order must have at least one node ,so just add
		frame_list_add_head(buddy_pmm.buckets[tmp_order].pages, right_child,
							child_order_avaliable_header);

		tmp_order -= 1;
	}
	pr_info("end while\n");
	/*third,try to del the node at the head of the alloc order list*/
	avaliable_header =
		(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number, tmp_order);
	header = (struct page_frame *)GET_HEAD_PTR(zone_number, tmp_order);
	if (!header)
		return -ENOMEM;
	pr_info("pmm alloc zone 1\n");
	del_node = avaliable_header;
	index = del_node - header;
	pr_info("pmm alloc zone 2 index %x\n",index);
	frame_list_del_init(buddy_pmm.buckets[tmp_order].pages, del_node);
	pr_info("pmm alloc zone 3\n");
	if (frame_list_only_one(buddy_pmm.buckets[tmp_order].pages, del_node))
		buddy_pmm.zone[zone_number].avaliable_frame[tmp_order] = NULL;
	pr_info("pmm alloc zone 4\n");
	/*Forth,mark all the child node alloced*/
	if (mark_childs(zone_number, tmp_order, index))
		return -ENOMEM;

	buddy_pmm.zone[zone_number].zone_total_avaliable_pages -= 1 << alloc_order;
	pr_debug("we alloced %x pages and have 0x%x pages after alloc\n",1<<alloc_order,buddy_pmm.zone[zone_number].zone_total_avaliable_pages);
	return PPN_FROM_IDX(alloc_order, index);
}
u32 pmm_alloc(size_t page_number) {
	int zone_number = ZONE_NORMAL;
	/*have we used too many physical memory*/
	if (page_number < 0)
		return 0;
	if (buddy_pmm.zone[zone_number].zone_total_avaliable_pages < page_number) {
		pr_error("this zone have no memory to alloc\n");
		/*TODO:if so ,we need to swap the memory*/
		return -ENOMEM;
	}
	return pmm_alloc_zone(page_number, zone_number);
}
static bool inline ppn_inrange(u32 ppn, int *zone_number) {
	struct buddy_zone mem_zone;
	bool ppn_inrange = false;
	for (int zone_n = 0; zone_n < ZONE_NR_MAX; ++zone_n) {
		mem_zone = buddy_pmm.zone[zone_n];
		if (PPN(mem_zone.zone_lower_addr) <= ppn &&
			PPN(mem_zone.zone_upper_addr) > ppn) {
			*zone_number = zone_n;
			ppn_inrange = true;
		}
	}
	return ppn_inrange;
}
static int pmm_free_one(u32 ppn) {
	u64 index, buddy_index;
	int tmp_order = 0, zone_number = 0;
	struct page_frame *avaliable_header = NULL, *header = NULL,
					  *insert_node = NULL;
	struct page_frame *buddy_node = NULL;

	/*try to insert the node and try to merge*/
	while (tmp_order <= BUDDY_MAXORDER) {
		index = IDX_FROM_PPN(tmp_order, ppn);
		avaliable_header =
			(struct page_frame *)GET_AVALI_HEAD_PTR(zone_number, tmp_order);
		header = GET_HEAD_PTR(zone_number, tmp_order);
		buddy_index = (index >> 1) << 1;
		buddy_index =
			(buddy_index == index) ? (buddy_index + 1) : (buddy_index);
		buddy_node = &(header[buddy_index]);
		insert_node = &(header[index]);
		/* if this node is original not alloced,
		 * just ignore and not add avaliable_pages
		 * we only count the order is 0 page
		 */
		if ((!tmp_order) && (insert_node->flags & PAGE_FRAME_ALLOCED))
			buddy_pmm.zone[zone_number].zone_total_avaliable_pages++;

		insert_node->flags &= ~PAGE_FRAME_ALLOCED;
		/*if buddy is not empty ,stop merge,and insert current node into the
		 * avaliable list*/
		if (buddy_node->flags & PAGE_FRAME_ALLOCED ||
			tmp_order == BUDDY_MAXORDER) {
			if (!avaliable_header)
				avaliable_header = insert_node;
			else {
				frame_list_add_head(buddy_pmm.buckets[tmp_order].pages,
									insert_node, avaliable_header);
			}

			break;
		}
		/*else try merge*/
		frame_list_del_init(buddy_pmm.buckets[tmp_order].pages, buddy_node);
		if (frame_list_only_one(buddy_pmm.buckets[tmp_order].pages, buddy_node))
			buddy_pmm.zone[zone_number].avaliable_frame[tmp_order] = NULL;
		tmp_order++;
	}
	return 0;
}
int pmm_free(u32 ppn, size_t page_number) {
	int alloc_order = 0;
	int free_one_result = 0;
	int zone_number = 0;
	if (ppn == -ENOMEM)
		return -ENOMEM;

	alloc_order = calculate_alloc_order(page_number);
	for (int page_count = 0; page_count < (1 << alloc_order); page_count++) {
		if (ppn_inrange(ppn + page_count, &zone_number) == false) {
			pr_error("this ppn is illegal\n");
			return -ENOMEM;
		}
		/*check whether this page is shared, and if it is shared,just check*/
		struct page_frame *header = GET_HEAD_PTR(zone_number, 0);
		struct page_frame *insert_node = &(header[ppn + page_count]);
		if (insert_node->shared_count != 0) {
			/*TODO*/
			;
		}
		if ((free_one_result = pmm_free_one(ppn + page_count)))
			return free_one_result;
	}
	pr_debug("after free we have 0x%x pages\n",buddy_pmm.zone[zone_number].zone_total_avaliable_pages);
	return 0;
}
struct buddy buddy_pmm = {.m_regions = &m_regions,
						  .pmm_init = pmm_init,
						  .pmm_alloc = pmm_alloc,
						  .pmm_free = pmm_free};
