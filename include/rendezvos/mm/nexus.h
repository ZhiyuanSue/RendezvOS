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
 * Layout (LP64): keep `struct nexus_node` compact; size depends on pointers.
 * NEXUS_PER_PAGE = PAGE_SIZE / sizeof(struct nexus_node) (integer divide;
 * a page may have trailing slack). Prefix: 3*list_entry + vs_common pointer.
 * vs_common points at VS_Common (see vmm.h): tag + anonymous union (by `type`).
 */
struct nexus_node {
        /*
         * manage_free_list vs cache_data union:
         * - For manager nodes: used as manage_free_list (linked list of manager
         * pages)
         * - For normal nodes: can be repurposed as cache_data for temporary
         * operations IMPORTANT: When using as cache_data, the function must
         * hold vspace lock to prevent is_page_manage_node() checks while fields
         * are repurposed.
         */
        union {
                struct list_entry manage_free_list; // Manager node usage
                struct {
                        ppn_t cached_ppn; // Cached physical page number
                        ENTRY_FLAGS_t cached_flags; // Cached original flags for
                                                    // rollback
                } cache_data;
        };
        struct list_entry aux_list;
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
                        /* root node*/
                        struct rb_root _vspace_rb_root;
                        /*The nexus lock is used to protect the vspace's
                         * register*/
                        cas_lock_t nexus_lock;
                };
        };
};

static inline VS_Common* nexus_node_vspace(const struct nexus_node* nexus_node)
{
        if (!nexus_node || !nexus_node->vs_common)
                return NULL;
        switch ((enum vs_common_kind)nexus_node->vs_common->type) {
        case VS_COMMON_KERNEL_HEAP_REF:
                return nexus_node->vs_common->vs;
        case VS_COMMON_TABLE_VSPACE:
                return nexus_node->vs_common;
        default:
                return NULL;
        }
}
static inline VS_Common*
nexus_root_heap_ref(const struct nexus_node* nexus_root)
{
        if (!nexus_root || !vs_common_is_heap_ref(nexus_root->vs_common))
                return NULL;
        return nexus_root->vs_common;
}

extern struct nexus_node* nexus_root;
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
struct nexus_node* init_nexus(struct map_handler* handler);
/*vspace*/
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VS_Common* vs);

typedef u64 vspace_clone_flags_t;

error_t vspace_clone(VS_Common* src_vs, VS_Common** dst_vs_out,
                     vspace_clone_flags_t flags, struct nexus_node* nexus_root);

/* nexus_root: per-CPU root from init_nexus (owns _vspace_rb_root). Not the node
 * returned by nexus_create_vspace_root_node (that is the per-vspace root). */
void nexus_delete_vspace(struct nexus_node* nexus_root, VS_Common* vs);
void nexus_migrate_vspace(struct nexus_node* src_nexus_root,
                          struct nexus_node* dst_nexus_root, VS_Common* vs);

/*page*/
/*
 * @brief Allocate and map free pages for kernel or user space
 *
 * @param page_num: number of pages to allocate
 * @param target_vaddr: target virtual address (hint for allocation)
 * @param nexus_root: per-CPU nexus root
 * @param vs: vspace for user pages (NULL for kernel space)
 * @param flags: page table flags for mapped pages
 *
 * @return void*: allocated virtual address, or NULL on failure
 *
 * @note For kernel space, uses per-CPU nexus_root with KERNEL_HEAP_REF.
 *       For user space, requires valid vs with TABLE_VSPACE.
 */
void* get_free_page(size_t page_num, vaddr target_vaddr,
                    struct nexus_node* nexus_root, VS_Common* vs,
                    ENTRY_FLAGS_t flags);

/*
 * @brief Free pages and unmap them from vspace
 *
 * @param p: starting virtual address to free
 * @param page_num: number of pages to free
 * @param vs: vspace containing the pages (NULL for kernel space)
 * @param nexus_root: per-CPU nexus root
 *
 * @return error_t: REND_SUCCESS on success, error code on failure
 *
 * @note Cleans up both nexus tree entries and page table mappings.
 *       For kernel space, uses per-CPU nexus_root with KERNEL_HEAP_REF.
 */
error_t free_pages(void* p, int page_num, VS_Common* vs,
                   struct nexus_node* nexus_root);

error_t user_fill_range(struct nexus_node* first_entry, int page_num,
                        struct nexus_node* vspace_node);
error_t user_unfill_range(void* p, int page_num, VS_Common* vs,
                          struct nexus_node* vspace_node);
error_t unfill_phy_page(MemZone* zone, ppn_t ppn, u64 new_entry_addr);

/*
 * @brief Update flags for an already-mapped user range with full rollback.
 *
 * mode == NEXUS_RANGE_FLAGS_ABSOLUTE:
 *   desired = set_mask (new_flags)
 *
 * mode == NEXUS_RANGE_FLAGS_DELTA:
 *   desired = (old | set_mask) & ~clear_mask
 */
typedef enum {
        NEXUS_RANGE_FLAGS_ABSOLUTE = 0,
        NEXUS_RANGE_FLAGS_DELTA = 1,
} nexus_range_flags_mode_t;

/*
 * @brief Update mapping flags on a user vspace range with full rollback support
 *
 * @param nexus_root: per-CPU nexus root for vspace lookup
 * @param vs: user vspace to update (must be VS_COMMON_TABLE_VSPACE)
 * @param start_addr: start of range to update (must be page-aligned)
 * @param length: length of range in bytes (will be rounded up to page size)
 * @param new_flags: new page table flags to apply
 *
 * @return error_t: REND_SUCCESS on success, error code on failure
 *
 * @ Semantics:
 * - Range is [start_addr, start_addr + length), page-aligned.
 * - All pages in range must already be mapped in `vs`'s nexus tree.
 * - Updates both nexus_node::region_flags and page-table entries.
 * - Either all pages are updated successfully, or all are restored to original
 * state.
 *
 * @note This is a generic "nexus as truth source" operation with full rollback.
 *       Remains Linux-agnostic (used by Linux mprotect, COW bookkeeping, etc.).
 *
 * @note This function repurposes manage_free_list as cache_data during
 * execution. The vspace lock ensures no concurrent is_page_manage_node() checks
 * will misinterpret the cached data. All fields are restored before lock
 * release.
 */
error_t nexus_update_range_flags(struct nexus_node* nexus_root,
                                 VS_Common* vs,
                                 vaddr start_addr,
                                 u64 length,
                                 nexus_range_flags_mode_t mode,
                                 ENTRY_FLAGS_t set_mask,
                                 ENTRY_FLAGS_t clear_mask);

/*
 * @brief Remap one existing user 4K leaf: change PTE ppn and/or flags.
 */
error_t nexus_remap_user_leaf(VS_Common* vs, vaddr va, ppn_t new_ppn,
                              ENTRY_FLAGS_t new_flags, ppn_t expect_old_ppn);

/* Kernel VA -> owning CPU for kmem routing: walks rmap under pmm lock; kernel
 * heap uses `cpu_id` on KERNEL_HEAP_REF vs_common (not global root). */
int nexus_kernel_page_owner_cpu(vaddr kva);
#endif