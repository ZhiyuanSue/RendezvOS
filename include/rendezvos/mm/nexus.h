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
        struct list_entry _vspace_list;
        VSpace* vs;
        union {
                /* manager node */
                struct {
                        struct rb_node _rb_node;
                        vaddr start_addr;
                        vaddr ppn;
                        u64 size;
                        u64 page_left_nexus;
                };
                /* root node*/
                struct {
                        struct rb_node _vspace_rb_node;
                        struct rb_root _rb_root;
                        struct rb_root _vspace_rb_root;
                        void* backup_manage_page;
                        struct map_handler* handler;
                        i64 nexus_id; /*should alloced by the upper level code*/
                };
        };
};
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
struct nexus_node* init_nexus(struct map_handler* handler);
/*vspace*/
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VSpace* vs);
void nexus_delete_vspace(struct nexus_node* nexus_root, VSpace* vs);

/*page*/
void* get_free_page(int page_num, enum zone_type memory_zone,
                    vaddr target_vaddr, struct nexus_node* nexus_root,
                    VSpace* vs, ENTRY_FLAGS_t flags);
error_t free_pages(void* p, int page_num, VSpace* vs,
                   struct nexus_node* nexus_root);

#endif