#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/buddy_pmm.h>
#include <common/string.h>
#include <common/limits.h>

struct buddy buddy_pmm;
extern struct memory_regions m_regions;
// u64 entry_per_bucket[BUDDY_MAXORDER + 1], pages_per_bucket[BUDDY_MAXORDER +
// 1];
DEFINE_PER_CPU(struct spin_lock_t, pmm_spin_lock);
size_t calculate_manage_space(size_t zone_page_number)
{
        // u64 pages;
        // u64 size_in_this_order;

        // pages = 0;
        // for (int order = 0; order <= BUDDY_MAXORDER; ++order)
        //         entry_per_bucket[order] = pages_per_bucket[order] = 0;

        /*we promised that this phy mem end is 2m aligned*/
        // for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
        //         size_in_this_order = (PAGE_SIZE << order);
        //         entry_per_bucket[order] = zone_page_number /
        //         size_in_this_order; pages_per_bucket[order] =
        //                 ROUND_UP(entry_per_bucket[order]
        //                                  * sizeof(struct buddy_page),
        //                          PAGE_SIZE)
        //                 / PAGE_SIZE;
        // }

        // for (int order = 0; order <= BUDDY_MAXORDER; ++order)
        //         pages += pages_per_bucket[order];
        return zone_page_number * sizeof(struct buddy_page);
}
void pmm_init(struct pmm *pmm, paddr pmm_phy_start_addr, paddr pmm_phy_end_addr)
{
        MemZone *zone = pmm->zone;
        struct buddy *bp = (struct buddy *)pmm;

        u64 order_page_size, child_order_page_size;
        size_t index = 0;

        /*generate the buddy bucket*/
        for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
                bp->buckets[order].order = order;
                INIT_LIST_HEAD(&bp->buckets[order].avaliable_frame_list);
        }

        bp->buddy_page_number = zone->zone_total_pages;
        bp->pages = (struct buddy_page *)KERNEL_PHY_TO_VIRT(pmm_phy_start_addr);
        bp->total_avaliable_pages = 0;
        if (pmm_phy_end_addr - pmm_phy_start_addr
            < bp->buddy_page_number * sizeof(struct buddy_page)) {
                pr_error(
                        "[ ERROR ]alloc buddy page space error, expect %d alloc %d!\n",
                        bp->buddy_page_number,
                        pmm_phy_end_addr - pmm_phy_start_addr);
                return;
        }
        for_each_sec_of_zone(bp->zone)
        {
                for_each_page_of_sec(sec)
                {
                        bp->pages[index].ppn =
                                sec->lower_addr
                                + (page - &sec->pages[0]) * PAGE_SIZE;
                        INIT_LIST_HEAD(&bp->pages[index].page_list);
                        index++;
                }
        }
        if (index != bp->buddy_page_number) {
                pr_error(
                        "[ ERROR ]buddy page number calculate error, expect %d real %d!\n",
                        bp->buddy_page_number,
                        index);
                return;
        }

        for (int order = 0; order <= BUDDY_MAXORDER; ++order) {
                if (order == 0) {
                        for (index = 0; index < bp->buddy_page_number;
                             index++) {
                                if (!(Zone_phy_Page(zone, index)->flags
                                      & PAGE_FRAME_USED)) {
                                        list_add_tail(
                                                &bp->pages[index].page_list,
                                                &bp->buckets[order]
                                                         .avaliable_frame_list);
                                        bp->pages[index].order = 0;
                                        bp->total_avaliable_pages++;
                                }
                        }
                } else {
                        order_page_size = (PAGE_SIZE << order);
                        child_order_page_size = (PAGE_SIZE << (order - 1));
                        for (index = 0; index < bp->buddy_page_number;
                             index++) {
                                if (ALIGNED(bp->pages[index].ppn,
                                            order_page_size)
                                    && index + (1 << (order - 1))
                                               < bp->buddy_page_number
                                    && bp->pages[index + (1 << (order - 1))].ppn
                                               == bp->pages[index].ppn
                                                          + child_order_page_size) {
                                        list_del_init(
                                                &bp->pages[index].page_list);
                                        list_del_init(
                                                &bp->pages[index
                                                           + (1 << (order - 1))]
                                                         .page_list);
                                        bp->pages[index].order++;
                                        list_add_tail(
                                                &bp->pages[index].page_list,
                                                &bp->buckets[order]
                                                         .avaliable_frame_list);
                                }
                        }
                }
        }
        return;
}
void clean_pmm_region(paddr pmm_data_phy_start, paddr pmm_data_phy_end)
{
        memset((void *)pmm_data_phy_start,
               0,
               pmm_data_phy_end - pmm_data_phy_start);
}

i64 pmm_alloc_zone(struct buddy *bp, int alloc_order,
                   size_t *alloced_page_number)
{
        bool find_an_order;
        int tmp_order;
        struct buddy_page *del_node, *left_child, *right_child;
        struct list_entry *avaliable_header;
        find_an_order = false;
        tmp_order = alloc_order;

        for (; tmp_order <= BUDDY_MAXORDER; tmp_order++) {
                avaliable_header = &bp->buckets[tmp_order].avaliable_frame_list;
                if (avaliable_header != avaliable_header->next) {
                        find_an_order = true;
                        break;
                }
        }

        if (!find_an_order) {
                pr_info("not find an order\n");
                *alloced_page_number = 0;
                return (-E_RENDEZVOS);
        }

        del_node = container_of(
                avaliable_header->next, struct buddy_page, page_list);
        while (del_node->order > alloc_order) {
                left_child = del_node;
                right_child = del_node + (1 << (del_node->order - 1));

                list_del_init(&del_node->page_list);
                del_node->order--;

                list_add_head(
                        &left_child->page_list,
                        &bp->buckets[del_node->order].avaliable_frame_list);
                list_add_head(
                        &right_child->page_list,
                        &bp->buckets[del_node->order].avaliable_frame_list);
        }
        list_del_init(&del_node->page_list);
        Zone_phy_Page(bp->zone, del_node - &bp->pages[0])->ref_count++;

        bp->total_avaliable_pages -= 1ULL << ((u64)alloc_order);
        *alloced_page_number = 1ULL << ((u64)alloc_order);

        return del_node->ppn;
}
i64 pmm_alloc(struct pmm *pmm, size_t page_number, size_t *alloced_page_number)
{
        u32 alloc_order;
        // MemZone *zone = pmm->zone;
        struct buddy *bp = (struct buddy *)pmm;

        /*have we used too many physical memory*/
        if (page_number < 0) {
                *alloced_page_number = 0;
                return (0);
        }

        if (bp->total_avaliable_pages < page_number) {
                pr_error("[ BUDDY ]this zone have no memory to alloc\n");
                /*TODO:if so ,we need to swap the memory*/
                *alloced_page_number = 0;
                return (-E_RENDEZVOS);
        }

        if (page_number > (1 << BUDDY_MAXORDER)) {
                *alloced_page_number = 0;
                return (-E_RENDEZVOS);
        }

        /*calculate the upper 2^n size*/
        alloc_order = log2_of_next_power_of_two(page_number);
        return (pmm_alloc_zone(bp, alloc_order, alloced_page_number));
}
static error_t pmm_free_one(struct pmm *pmm, i64 ppn)
{
        struct buddy *bp = (struct buddy *)pmm;
        int tmp_order = 0;
        i64 index, buddy_index;
        struct buddy_page *buddy_node, *insert_node;
        MemZone *zone = bp->zone;
        index = ppn_Zone_index(zone, ppn);
        if (index < 0)
                return (-E_RENDEZVOS);
        insert_node = &bp->pages[index];
        if (!insert_node)
                return (-E_RENDEZVOS);
        /*try to insert the node and try to merge*/
        while (tmp_order <= BUDDY_MAXORDER) {
                if (tmp_order == BUDDY_MAXORDER) {
                        insert_node->order = tmp_order;
                        list_add_head(
                                &insert_node->page_list,
                                &bp->buckets[tmp_order].avaliable_frame_list);
                }
                index = insert_node - bp->pages;
                if ((insert_node->ppn >> tmp_order) % 2) {
                        buddy_index = index + (1 << tmp_order);
                } else {
                        buddy_index = index - (1 << tmp_order);
                }
                /*the buddy is out of range, means no buddy*/
                if (buddy_index < 0 || buddy_index > bp->buddy_page_number) {
                        list_add_head(
                                &insert_node->page_list,
                                &bp->buckets[tmp_order].avaliable_frame_list);
                        break;
                }
                /*find a page in another section*/
                buddy_node = &bp->pages[buddy_index];
                if ((buddy_node > insert_node
                     && buddy_node->ppn - insert_node->ppn != 1 << tmp_order)
                    || (buddy_node < insert_node
                        && insert_node->ppn - buddy_node->ppn
                                   != 1 << tmp_order)) {
                        list_add_head(
                                &insert_node->page_list,
                                &bp->buckets[tmp_order].avaliable_frame_list);
                        break;
                }

                /*the buddy is still alloced*/
                if (buddy_node->page_list.next == buddy_node->page_list.prev) {
                        list_add_head(
                                &insert_node->page_list,
                                &bp->buckets[tmp_order].avaliable_frame_list);
                        break;
                }

                /*merge*/
                list_del(&insert_node->page_list);
                list_del(&buddy_node->page_list);

                /*next level*/
                insert_node = &bp->pages[MIN(index, buddy_index)];

                tmp_order++;
        }

        return (0);
}
error_t pmm_free(struct pmm *pmm, i64 ppn, size_t page_number)
{
        u32 alloc_order;
        int free_one_result;

        if (ppn == -E_RENDEZVOS)
                return (-E_RENDEZVOS);

        alloc_order = log2_of_next_power_of_two(page_number);
        for (i64 page_count = 0; page_count < (1 << alloc_order);
             page_count++) {
                if (ppn_in_Zone(pmm->zone, ppn + page_count) == false) {
                        pr_error("[ BUDDY ]this ppn is illegal\n");
                        return (-E_RENDEZVOS);
                }

                /*check whether this page is shared, and if it is shared,just
                 * check*/

                ppn_Zone_phy_Page(pmm->zone, ppn + page_count)->ref_count--;
                if (ppn_Zone_phy_Page(pmm->zone, ppn + page_count)->ref_count
                    > 0) {
                        continue;
                }

                if ((free_one_result = pmm_free_one(pmm, ppn + page_count)))
                        return (free_one_result);
        }

        return (0);
}
struct buddy buddy_pmm = {.pmm_init = pmm_init,
                          .pmm_alloc = pmm_alloc,
                          .pmm_free = pmm_free,
                          .pmm_calculate_manage_space = calculate_manage_space,
                          .spin_ptr = NULL};
