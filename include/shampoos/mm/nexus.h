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
                        struct list_entry _free_list;
                        vaddr start_addr;
                        u32 size;
                        u32 page_left_node;
                } __attribute__((__packed__));
                /* root node*/
                struct {
                        struct rb_root _rb_root;
                        vaddr backup_manage_page;
                        struct pmm* pmm;
                };
        };
};
void init_nexus();
void get_free_page(int order, enum zone_type memory_zone);
void free_pages(void* p);

#endif