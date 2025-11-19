#ifndef _RENDEZVOS_NEXUS_H_
#define _RENDEZVOS_NEXUS_H_
/* This is a simple virture page allocator,  and the designation is in the
 * docs/mm.md */

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/sync/cas_lock.h>
struct nexus_node {
        struct list_entry manage_free_list;
        struct list_entry _free_list;
        struct list_entry _vspace_list;
        VSpace* vs;
        union {
                struct {
                        struct rb_node _rb_node;
                        ENTRY_FLAGS_t region_flags;
                        vaddr addr;
                        union {
                                /* manager node */
                                u64 page_left_nexus;
                                /* normal node */
                                struct list_entry rmap_list;
                        };
                };
                /*the vspace root and all nexus root node*/
                struct {
                        /*common*/
                        struct rb_node _vspace_rb_node;
                        struct rb_root _rb_root;
                        struct map_handler* handler;
                        /* root node*/
                        struct rb_root _vspace_rb_root;
                        cas_lock_t nexus_lock;
                };
        };
};
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
struct nexus_node* init_nexus(struct map_handler* handler);
/*vspace*/
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VSpace* vs);
void nexus_delete_vspace(struct nexus_node* nexus_root, VSpace* vs);
void nexus_migrate_vspace(struct nexus_node* src_nexus_root,
                          struct nexus_node* dst_nexus_root, VSpace* vs);

/*page*/
void* get_free_page(int page_num, vaddr target_vaddr,
                    struct nexus_node* nexus_root, VSpace* vs,
                    ENTRY_FLAGS_t flags);
error_t free_pages(void* p, int page_num, VSpace* vs,
                   struct nexus_node* nexus_root);

error_t user_fill_range(struct nexus_node* first_entry, int page_num,
                        struct nexus_node* vspace_node, VSpace* vs);
error_t user_unfill_range(void* p, int page_num, VSpace* vs,
                          struct nexus_node* vspace_node);
#endif