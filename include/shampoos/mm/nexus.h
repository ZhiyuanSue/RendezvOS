#ifndef _SHAMPOOS_NEXUS_H_
#define _SHAMPOOS_NEXUS_H_
/* This is a simple virture page allocator,  and the designation is in the
 * docs/mm.md */

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <shampoos/mm/pmm.h>
struct nexus_node {
        union {
                /* manager node */
                struct {
                        struct rb_node _rb_node;
                        struct list_entry manage_free_list;
                        struct list_entry _free_list;
                        vaddr start_addr;
                        u64 size;
                        u64 page_left_node;
                };
                /* root node*/
                struct {
                        struct rb_root _rb_root;
                        struct list_entry manage_free_list_head;
                        void* backup_manage_page;
                        struct pmm* pmm;
                };
        };
};
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
error_t init_nexus();
error_t get_free_page(int order, enum zone_type memory_zone);
error_t free_pages(void* p);

#endif