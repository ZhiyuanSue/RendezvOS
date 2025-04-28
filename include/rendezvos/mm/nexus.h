#ifndef _RENDEZVOS_NEXUS_H_
#define _RENDEZVOS_NEXUS_H_
/* This is a simple virture page allocator,  and the designation is in the
 * docs/mm.md */

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
struct nexus_node {
        struct list_entry manage_free_list;
        struct list_entry _free_list;
        union {
                /* manager node */
                struct {
                        struct rb_node _rb_node;
                        vaddr start_addr;
                        vaddr ppn;
                        paddr vspace_root;
                        u64 size;
                        u64 page_left_nexus;
                };
                /* root node*/
                struct {
                        struct rb_root _rb_root;
                        void* backup_manage_page;
                        struct map_handler* handler;
                        int nexus_id; /*should alloced by the upper level code*/
                };
        };
};
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
struct nexus_node* init_nexus(struct map_handler* handler);
void* get_free_page(int page_num, enum zone_type memory_zone,
                    vaddr target_vaddr, struct vspace* vs,
                    struct nexus_node* nexus_root);
error_t free_pages(void* p, int page_num, struct vspace* vs,
                   struct nexus_node* nexus_root);
struct nexus_node* nexus_rb_tree_search(struct rb_root* root, vaddr start_addr,
                                        paddr vspace_root);

#endif