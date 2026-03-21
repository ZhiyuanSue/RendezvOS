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
#include <rendezvos/limits.h>
/*
 * Layout (LP64): `sizeof(struct nexus_node)` is 120 bytes (no padding to 128).
 * NEXUS_PER_PAGE = PAGE_SIZE / sizeof(struct nexus_node) (integer divide;
 * a page may have trailing slack). Prefix: 3*list_entry + vs_common pointer.
 * vs_common points at VS_Common (see vmm.h): tag + anonymous union (by `type`).
 */
struct nexus_node {
        struct list_entry manage_free_list;
        struct list_entry _free_list;
        struct list_entry _vspace_list;
        VS_Common* vs_common;
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

/*
 * One type, several logical roles: same layout may be a tree root, a subtree
 * root, a bookkeeping slot, or a leaf record—callers must name pointers by
 * role where it matters. These helpers only read `vs_common` / `VS_Common`
 * (discriminated by `vs_common->type`).
 */
static inline bool nexus_node_vs_is_kernel_kref(const struct nexus_node* nexus_node)
{
        return nexus_node->vs_common
               && nexus_node->vs_common->type
                          == (u64)VS_COMMON_KERNEL_HEAP_REF;
}

static inline VS_Common* nexus_node_vspace(const struct nexus_node* nexus_node)
{
        if (!nexus_node->vs_common)
                return NULL;
        switch ((enum vs_common_kind)nexus_node->vs_common->type) {
        case VS_COMMON_KERNEL_HEAP_REF:
                return nexus_node->vs_common->vs;
        case VS_COMMON_USER_VSPACE:
                return nexus_node->vs_common;
        default:
                return NULL;
        }
}

extern struct nexus_node* nexus_root;
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
struct nexus_node* init_nexus(struct map_handler* handler);
/*vspace*/
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VS_Common* vs);
/* nexus_root: per-CPU root from init_nexus (owns _vspace_rb_root). Not the node
 * returned by nexus_create_vspace_root_node (that is the per-vspace root). */
void nexus_delete_vspace(struct nexus_node* nexus_root, VS_Common* vs);
void nexus_migrate_vspace(struct nexus_node* src_nexus_root,
                          struct nexus_node* dst_nexus_root, VS_Common* vs);

/*page*/
void* get_free_page(size_t page_num, vaddr target_vaddr,
                    struct nexus_node* nexus_root, VS_Common* vs,
                    ENTRY_FLAGS_t flags);
error_t free_pages(void* p, int page_num, VS_Common* vs,
                   struct nexus_node* nexus_root);

error_t user_fill_range(struct nexus_node* first_entry, int page_num,
                        struct nexus_node* vspace_node, VS_Common* vs);
error_t user_unfill_range(void* p, int page_num, VS_Common* vs,
                          struct nexus_node* vspace_node);
error_t unfill_phy_page(MemZone* zone, ppn_t ppn, u64 new_entry_addr);

/* Kernel VA -> owning CPU for kmem routing: walks rmap under pmm lock; kernel
 * heap uses `cpu_id` on KERNEL_HEAP_REF vs_common (not global root). */
int nexus_kernel_page_owner_cpu(vaddr kva);
#endif