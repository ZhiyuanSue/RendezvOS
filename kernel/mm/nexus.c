#include <common/string.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>

/*
 * Page.rmap_list is part of PMM-owned metadata: link/unlink must run under the
 * same zone pmm MCS lock as readers (nexus_kernel_page_owner_cpu,
 * unfill_phy_page snapshot). Mapping paths hold the page-table vspace lock
 * (see below) before calling link_rmap_list, so lock order is always
 * that lock -> pmm (no pmm while holding only nexus in the inverse order for
 * the same page).
 *
 * Kernel kmem: all CPUs share root_vspace page tables. Per-CPU nexus_root uses
 * KERNEL_HEAP_REF whose nexus_vspace_lock protects only that CPU's RB tree.
 * Any map/unmap/have_mapped on root_vspace is serialized by
 * root_vspace.vspace_lock (taken inside map/unmap/have_mapped via per-CPU
 * map_handler). Order when both apply: heap_ref lock (kernel nexus tree),
 * then page-table ops (vspace_lock) (never reverse).
 */
static inline void link_rmap_list(MemZone* zone, ppn_t ppn,
                                  struct nexus_node* node)
{
        ZonePageCursor cur;
        Page* p_ptr = zone_page_cursor_init(&cur, zone, ppn);
        if (!p_ptr) {
                pr_error("[ NEXUS ] link_rmap_list: invalid ppn\n");
                return;
        }
        pmm_zone_lock(zone);
        /*
                because nexus might have 2M and 4K range, here we must check,
                actually, which should not be alloced successfully before,
                but we still need to check here.
                if rmap_list have no nexus node linked, just link,
                otherwise we need to check, is the ppn 2M aligned? if not, just
           link, if so, check the head nexus 2M or not?
        */
        if (!ALIGNED(ppn, MIDDLE_PAGES)) {
                list_add_head(&node->rmap_list, &p_ptr->rmap_list);
        } else {
                struct list_entry* list_node = p_ptr->rmap_list.next;
                if (list_node == &p_ptr->rmap_list) {
                        list_add_head(&node->rmap_list, &p_ptr->rmap_list);
                } else {
                        struct nexus_node* header_node = container_of(
                                list_node, struct nexus_node, rmap_list);
                        if ((header_node->region_flags & PAGE_ENTRY_HUGE)
                            == (node->region_flags & PAGE_ENTRY_HUGE)) {
                                list_add_head(&node->rmap_list,
                                              &p_ptr->rmap_list);
                        } else {
                                pr_error(
                                        "[ NEXUS ] try to map a 2M and a 4K page on a 2M aligned phy page\n");
                        }
                }
        }
        pmm_zone_unlock(zone);
}
static inline void unlink_rmap_list(MemZone* zone, ppn_t ppn,
                                    struct nexus_node* node)
{
        ZonePageCursor cur;
        if (!zone_page_cursor_init(&cur, zone, ppn))
                return;
        pmm_zone_lock(zone);
        list_del_init(&node->rmap_list);
        pmm_zone_unlock(zone);
}
static inline u64 nexus_node_get_len(struct nexus_node* nexus_entry)
{
        return (nexus_entry->region_flags & PAGE_ENTRY_HUGE) ?
                       MIDDLE_PAGE_SIZE :
                       PAGE_SIZE;
}
static inline u64 nexus_node_get_pages(struct nexus_node* nexus_entry)
{
        return (nexus_entry->region_flags & PAGE_ENTRY_HUGE) ? MIDDLE_PAGES : 1;
}
static inline void nexus_node_set_len(struct nexus_node* nexus_entry,
                                      bool is_2M)
{
        if (is_2M)
                nexus_entry->region_flags |= PAGE_ENTRY_HUGE;
}

/*
 * Apply a 4K user leaf update with nexus as truth source.
 *
 * - If ppn unchanged: update PTE flags + nexus_node::region_flags.
 * - If ppn changed: unlink rmap(old), remap leaf (PAGE_ENTRY_REMAP),
 *   update nexus_node::region_flags, link rmap(new).
 *
 * Caller must hold vs->nexus_vspace_lock and ensure:
 * - `vs` is VS_COMMON_TABLE_VSPACE
 * - `node` is non-huge and node->addr is page-aligned
 */
static error_t nexus_update_node(VS_Common* vs, struct map_handler* handler,
                                 struct pmm* pmm_ptr, struct nexus_node* node,
                                 ppn_t old_ppn, ppn_t new_ppn,
                                 ENTRY_FLAGS_t desired_flags)
{
        ENTRY_FLAGS_t store_flags =
                clear_mask_u64(desired_flags, PAGE_ENTRY_REMAP);

        if (new_ppn == old_ppn) {
                if (map(vs, old_ppn, VPN(node->addr), 3, desired_flags, handler)
                    != REND_SUCCESS)
                        return -E_RENDEZVOS;
                node->region_flags = store_flags;
                return REND_SUCCESS;
        }

        if (!pmm_ptr || !pmm_ptr->zone)
                return -E_RENDEZVOS;

        unlink_rmap_list(pmm_ptr->zone, old_ppn, node);
        error_t e = map(vs,
                        new_ppn,
                        VPN(node->addr),
                        3,
                        desired_flags | PAGE_ENTRY_REMAP,
                        handler);
        if (e != REND_SUCCESS) {
                link_rmap_list(pmm_ptr->zone, old_ppn, node);
                return e;
        }

        node->region_flags = store_flags;
        link_rmap_list(pmm_ptr->zone, new_ppn, node);
        /*
         * Drop one reference from the old physical page. buddy pmm_free
         * decrements refcount and only releases to buddy when it reaches 0.
         * The new page's refcount is owned by its allocator (COW split path
         * allocates a fresh page with refcount already incremented).
         */
        (void)pmm_ptr->pmm_free(pmm_ptr, old_ppn, 1);
        return REND_SUCCESS;
}
static void nexus_rb_tree_insert(struct nexus_node* node,
                                 struct rb_root* vspace_root)
{
        struct rb_node** new = &vspace_root->rb_root, *parent = NULL;
        u64 key = node->addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node =
                        container_of(parent, struct nexus_node, _rb_node);
                if (key < (u64)tmp_node->addr)
                        new = &parent->left_child;
                else if (key >= (u64)(tmp_node->addr
                                      + nexus_node_get_len(tmp_node)))
                        new = &parent->right_child;
                else
                        return;
        }
        RB_Link_Node(&node->_rb_node, parent, new);
        RB_SolveDoubleRed(&node->_rb_node, vspace_root);
}
static void nexus_rb_tree_vspace_insert(struct nexus_node* vspace_node,
                                        struct rb_root* vspace_rb_root)
{
        struct rb_node** new = &vspace_rb_root->rb_root, *parent = NULL;
        u64 key = vspace_node->vs_common->vspace_root_addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node = container_of(
                        parent, struct nexus_node, _vspace_rb_node);
                if (key < (u64)tmp_node->vs_common->vspace_root_addr)
                        new = &parent->left_child;
                else if (key > (u64)tmp_node->vs_common->vspace_root_addr)
                        new = &parent->right_child;
                else {
                        return;
                }
        }
        RB_Link_Node(&vspace_node->_vspace_rb_node, parent, new);
        RB_SolveDoubleRed(&vspace_node->_vspace_rb_node, vspace_rb_root);
}
static void nexus_rb_tree_remove(struct nexus_node* node,
                                 struct rb_root* vspace_root)
{
        RB_Remove(&node->_rb_node, vspace_root);
        node->_rb_node.black_height = node->_rb_node.rb_parent_color = 0;
        node->_rb_node.left_child = node->_rb_node.right_child = NULL;
}
static void nexus_rb_tree_vspace_remove(struct nexus_node* vspace_node,
                                        struct rb_root* root)
{
        RB_Remove(&vspace_node->_vspace_rb_node, root);
        vspace_node->_vspace_rb_node.black_height =
                vspace_node->_vspace_rb_node.rb_parent_color = 0;
        vspace_node->_vspace_rb_node.left_child =
                vspace_node->_vspace_rb_node.right_child = NULL;
}
struct nexus_node* nexus_rb_tree_vspace_search(struct rb_root* root,
                                               paddr vspace_root_addr)
{
        struct rb_node* node = root->rb_root;
        while (node) {
                struct nexus_node* tmp_node =
                        container_of(node, struct nexus_node, _vspace_rb_node);
                if (vspace_root_addr < tmp_node->vs_common->vspace_root_addr)
                        node = node->left_child;
                else if (vspace_root_addr
                         > tmp_node->vs_common->vspace_root_addr)
                        node = node->right_child;
                else
                        return tmp_node;
        }
        return NULL;
}
struct nexus_node* nexus_rb_tree_search(struct rb_root* vspace_root,
                                        vaddr start_addr)
{
        struct rb_node* node = vspace_root->rb_root;
        struct nexus_node* tmp_node = NULL;
        while (node) {
                tmp_node = container_of(node, struct nexus_node, _rb_node);
                if (start_addr < tmp_node->addr)
                        node = node->left_child;
                else if (start_addr >= (u64)(tmp_node->addr
                                             + nexus_node_get_len(tmp_node)))
                        node = node->right_child;
                else
                        return tmp_node;
        }
        return NULL;
}
struct nexus_node* nexus_rb_tree_prev(struct nexus_node* node)
{
        struct rb_node* curr_rb = &node->_rb_node;
        struct rb_node* prev_rb = RB_Prev(curr_rb);
        if (!prev_rb)
                return NULL;
        return container_of(prev_rb, struct nexus_node, _rb_node);
}
struct nexus_node* nexus_rb_tree_next(struct nexus_node* node)
{
        struct rb_node* curr_rb = &node->_rb_node;
        struct rb_node* next_rb = RB_Next(curr_rb);
        if (!next_rb)
                return NULL;
        return container_of(next_rb, struct nexus_node, _rb_node);
}
static inline bool is_page_manage_node(struct nexus_node* node)
{
        return node->manage_free_list.next && node->manage_free_list.prev;
}
static void nexus_init_manage_page(vaddr vpage_addr,
                                   struct nexus_node* vspace_root)
{
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;
        /*init the node 0 point to this page*/
        n_node->addr = vpage_addr;
        /*the manage page is promised in kernel space, and use a identity
         * mapping*/
        nexus_node_set_len(n_node, false);
        n_node->page_left_nexus = NEXUS_PER_PAGE - 1;
        /*init the list*/
        INIT_LIST_HEAD(&n_node->aux_list);
        for (u64 i = 1; i < NEXUS_PER_PAGE; i++) {
                list_add_head(&n_node[i].aux_list, &n_node->aux_list);
        }
        /*insert to rb tree*/
        list_add_head(&(n_node->_vspace_list), &(vspace_root->_vspace_list));
        nexus_rb_tree_insert(n_node, &vspace_root->_rb_root);
}
static struct nexus_node* init_vspace_nexus(vaddr nexus_page_addr,
                                            VS_Common* vs,
                                            struct map_handler* handler,
                                            struct rb_root* _vspace_rb_root)
{
        if (!vs || !handler || !ALIGNED(nexus_page_addr, PAGE_SIZE)) {
                pr_error("[ NEXUS ] ERROR: init vspace nexus input error\n");
                return NULL;
        }
        /*remember clean this page*/
        memset((void*)nexus_page_addr, '\0', PAGE_SIZE);
        struct nexus_node* n_node = (struct nexus_node*)nexus_page_addr;

        /*init the node 1 as the root and let the nexus_root point to it*/
        struct nexus_node* root_node = &n_node[1];
        if (vs == &root_vspace) {
                VS_Common* heap_ref =
                        &per_cpu(nexus_kernel_heap_vs_common, handler->cpu_id);
                heap_ref->type = (u64)VS_COMMON_KERNEL_HEAP_REF;
                heap_ref->vs = vs;
                heap_ref->cpu_id = (u32)handler->cpu_id;
                heap_ref->pmm = handler->pmm;
                n_node[1].vs_common = heap_ref;
                heap_ref->_vspace_node = (void*)&n_node[1];
        } else {
                n_node[1].vs_common = vs;
                vs->_vspace_node = (void*)&n_node[1];
                if (!vs->pmm)
                        vs->pmm = handler->pmm;
        }
        INIT_LIST_HEAD(&n_node[1].manage_free_list);
        INIT_LIST_HEAD(&n_node[1]._vspace_list);
        lock_init_cas(&n_node[1].nexus_lock);
        lock_init_cas(&n_node[1].vs_common->nexus_vspace_lock);
        if (_vspace_rb_root) {
                nexus_rb_tree_vspace_insert(&n_node[1], _vspace_rb_root);
        } else {
                nexus_rb_tree_vspace_insert(&n_node[1],
                                            &n_node[1]._vspace_rb_root);
        }

        nexus_init_manage_page(nexus_page_addr, &n_node[1]);
        /*you have to del the root node from the free list,for in init manage
         * page, it have been linked*/
        list_del_init(&n_node[1].aux_list);

        n_node->page_left_nexus -= 1;
        list_add_head(&n_node->manage_free_list, &root_node->manage_free_list);
        return &n_node[1];
}
/*return a nexus root node*/
struct nexus_node* init_nexus(struct map_handler* handler)
{
        if (!handler || !handler->pmm) {
                return NULL;
        }
        VS_Common* vs = per_cpu(current_vspace, handler->cpu_id);
        /*get a phy page*/
        size_t alloced_page_number;
        struct pmm* pmm_ptr = handler->pmm;
        ppn_t nexus_init_page =
                pmm_ptr->pmm_alloc(pmm_ptr, 1, &alloced_page_number);
        if (invalid_ppn(nexus_init_page) || alloced_page_number != 1) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                return NULL;
        }
        /*get a vir page with Identical mapping*/
        vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_init_page));
        if (map(vs,
                nexus_init_page,
                VPN(vpage_addr),
                3,
                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                        | PAGE_ENTRY_WRITE,
                handler)) {
                pr_error("[ NEXUS ] ERROR: init nexus map error\n");
                pmm_ptr->pmm_free(pmm_ptr, nexus_init_page, 1);
                return NULL;
        }
        return init_vspace_nexus(vpage_addr, vs, handler, NULL);
}
static struct nexus_node*
nexus_get_free_entry(struct nexus_node* vspace_root_node)
{
        if (!vspace_root_node) {
                return NULL;
        }
        size_t alloced_page_number;
        /*from manage_free_list find one manage page that have free node*/
        struct list_entry* manage_free_list_node =
                &vspace_root_node->manage_free_list;
        struct list_entry* lp = manage_free_list_node->next;
        if (lp == manage_free_list_node) {
                /*means no free manage can use, try alloc a new one*/
                VS_Common* vs = nexus_node_vspace(vspace_root_node);
                if (!vs || !vspace_root_node->vs_common
                    || !vspace_root_node->vs_common->pmm) {
                        pr_error("[ NEXUS ] ERROR: invalid vs or pmm\n");
                        return NULL;
                }

                struct pmm* pmm_ptr = vspace_root_node->vs_common->pmm;
                ppn_t nexus_new_page =
                        pmm_ptr->pmm_alloc(pmm_ptr, 1, &alloced_page_number);
                if (invalid_ppn(nexus_new_page) || alloced_page_number != 1) {
                        pr_error("[ NEXUS ] ERROR: init error\n");
                        return NULL;
                }
                vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_new_page));
                error_t map_res =
                        map(vs,
                            nexus_new_page,
                            VPN(vpage_addr),
                            3,
                            PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                    | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                            &percpu(Map_Handler));
                if (map_res) {
                        pr_error("[ NEXUS ] ERROR: get free entry map error\n");
                        pmm_ptr->pmm_free(pmm_ptr, nexus_new_page, 1);
                        return NULL;
                }
                memset((void*)vpage_addr, '\0', PAGE_SIZE);
                nexus_init_manage_page(vpage_addr, vspace_root_node);
                lp = &((struct nexus_node*)vpage_addr)->manage_free_list;
                /*insert the lp to the manage page list*/
                list_add_head(lp, &vspace_root_node->manage_free_list);
        }
        if (!lp || lp == manage_free_list_node) {
                pr_error("[ ERROR ]find an free manage page fail\n");
                return NULL;
        }
        /*here we promise we have a usable lp, then from lp get the manage page
         * mate info*/
        struct nexus_node* usable_manage_page =
                container_of(lp, struct nexus_node, manage_free_list);
        /*use the aux_list (free list) to find a new entry*/
        struct list_entry* usable_entry = usable_manage_page->aux_list.next;
        struct nexus_node* usable_manage_entry =
                container_of(usable_entry, struct nexus_node, aux_list);
        /*clean the entry,remember that we must del it first then clean the
         * entry*/
        list_del_init(usable_entry);
        usable_manage_page->page_left_nexus--;
        if (usable_manage_page->page_left_nexus == 0) {
                list_del_init(&usable_manage_page->manage_free_list);
        }
        memset(usable_manage_entry, '\0', sizeof(struct nexus_node));
        /*
         * Invariant: allocated/in-use nexus_node must keep aux_list detached
         * (self-linked) unless a local helper temporarily borrows it.
         *
         * We memset() the node before publishing it; re-init aux_list here so
         * list_node_is_detached() works for later temporary borrows.
         */
        INIT_LIST_HEAD(&usable_manage_entry->aux_list);
        return usable_manage_entry;
}
static void free_manage_node_with_page(struct nexus_node* page_manage_node,
                                       struct nexus_node* vspace_root)
{
        /*free this manage page*/
        ppn_t ppn = unmap(nexus_node_vspace(vspace_root),
                          VPN((vaddr)page_manage_node),
                          0,
                          &percpu(Map_Handler));
        if (ppn < 0) {
                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                return;
        }
        struct pmm* pmm_ptr =
                vspace_root->vs_common ? vspace_root->vs_common->pmm : NULL;
        if (!pmm_ptr) {
                pr_error("[ NEXUS ] ERROR: free manage page missing pmm\n");
                return;
        }
        error_t e = pmm_ptr->pmm_free(pmm_ptr, ppn, 1);
        if (e) {
                pr_error(
                        "[ Error ] pmm free error %d in free manage node with page %d\n",
                        e,
                        ppn);
        }
}
static void nexus_free_entry(struct nexus_node* nexus_entry,
                             struct nexus_node* nexus_root)
{
        struct nexus_node* page_manage_node =
                (struct nexus_node*)ROUND_DOWN((vaddr)nexus_entry, PAGE_SIZE);
        if (page_manage_node->page_left_nexus >= NEXUS_PER_PAGE) {
                pr_error(
                        "[ NEXUS ] unexpect case: a manage page have no entry to free but still try free it\n");
                return;
        }
        list_add_head(&nexus_entry->aux_list, &page_manage_node->aux_list);
        if (!page_manage_node->page_left_nexus) {
                list_add_head(&page_manage_node->manage_free_list,
                              &nexus_root->manage_free_list);
        }
        page_manage_node->page_left_nexus++;
        if (page_manage_node->page_left_nexus == NEXUS_PER_PAGE - 1) {
                /*after this del, this manage page is empty*/
                /*we also need to del it from the list and the rb tree
                 * first*/
                list_del_init(&page_manage_node->manage_free_list);
                list_del_init(&page_manage_node->_vspace_list);
                nexus_rb_tree_remove(page_manage_node, &nexus_root->_rb_root);
                free_manage_node_with_page(page_manage_node, nexus_root);
        }
}
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VS_Common* vs)
{
        struct nexus_node* res = NULL;
        /*try to find the vs paddr root ,if exist, error*/
        if (!nexus_root || !vs) {
                pr_error("[Error] input parameter error\n");
                goto fail;
        }
        lock_cas(&nexus_root->nexus_lock);
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (vspace_node) {
                pr_error("[Error] have has such a vspace in nexus\n");
                goto fail;
        }

        /*get a phy page*/
        size_t alloced_page_number;
        struct pmm* pmm_ptr =
                nexus_root->vs_common ? nexus_root->vs_common->pmm : NULL;
        if (!pmm_ptr) {
                pr_error("[Error] nexus_root missing pmm\n");
                goto fail;
        }
        ppn_t nexus_init_page =
                pmm_ptr->pmm_alloc(pmm_ptr, 1, &alloced_page_number);
        if (invalid_ppn(nexus_init_page) || alloced_page_number != 1) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                goto fail;
        }
        /*get a vir page with Identical mapping*/
        vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_init_page));
        if (map(vs,
                nexus_init_page,
                VPN(vpage_addr),
                3,
                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                        | PAGE_ENTRY_WRITE,
                &percpu(Map_Handler))) {
                pr_error("[ NEXUS ] ERROR: init nexus map error\n");
                if (!invalid_ppn(nexus_init_page)) {
                        pmm_ptr->pmm_free(pmm_ptr, nexus_init_page, 1);
                }
                goto fail;
        }
        res = init_vspace_nexus(vpage_addr,
                                vs,
                                &percpu(Map_Handler),
                                &nexus_root->_vspace_rb_root);
        if (res && vs) {
                vs->pmm = pmm_ptr;
                vs->cpu_id = (cpu_id_t)percpu(cpu_number);
        }
        if (!res) {
                unmap(vs, VPN(vpage_addr), 0, &percpu(Map_Handler));
                pmm_ptr->pmm_free(pmm_ptr, nexus_init_page, 1);
        }
        unlock_cas(&nexus_root->nexus_lock);
        return res;

fail:
        unlock_cas(&nexus_root->nexus_lock);
        return res;
}

void nexus_migrate_vspace(struct nexus_node* src_nexus_root,
                          struct nexus_node* dst_nexus_root, VS_Common* vs)
{
        if (!src_nexus_root || !dst_nexus_root || !vs) {
                pr_error("[Error] input parameter error\n");
                goto fail;
        }
        if (!vs->vspace_root_addr) {
                pr_error(
                        "[Error] we should not delete the kernel nexus vspace\n");
                goto fail;
        }
        /*try to find the vs paddr root ,if not, error*/
        lock_cas(&src_nexus_root->nexus_lock);
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &src_nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                unlock_cas(&src_nexus_root->nexus_lock);
                goto fail;
        }
        if (vs) {
                if (dst_nexus_root->vs_common) {
                        vs->cpu_id =
                                (cpu_id_t)dst_nexus_root->vs_common->cpu_id;
                        /*
                         * TODO:
                         * We intentionally do NOT switch vs->pmm here.
                         *
                         * In a multi-zone/NUMA design, vs->pmm denotes the
                         * default allocation policy for this address space.
                         * If we do not migrate existing pages, changing pmm
                         * would make later unmap/free paths use the wrong PMM
                         * metadata/locks for already-allocated pages.
                         *
                         * pmm switching must be paired with actual page
                         * migration (or with a per-page/per-region policy).
                         */
                } else {
                        vs->cpu_id = 0;
                }
        }
        nexus_rb_tree_vspace_remove(vspace_node,
                                    &(src_nexus_root->_vspace_rb_root));
        unlock_cas(&src_nexus_root->nexus_lock);

        lock_cas(&dst_nexus_root->nexus_lock);
        nexus_rb_tree_vspace_insert(vspace_node,
                                    &dst_nexus_root->_vspace_rb_root);
        unlock_cas(&dst_nexus_root->nexus_lock);
fail:
        return;
}

void nexus_delete_vspace(struct nexus_node* nexus_root, VS_Common* vs)
{
        if (!nexus_root || !vs) {
                pr_error("[Error] input parameter error\n");
                goto fail;
        }
        if (!vs->vspace_root_addr) {
                pr_error(
                        "[Error] we should not delete the kernel nexus vspace\n");
                goto fail;
        }
        struct pmm* pmm_ptr =
                nexus_root->vs_common ? nexus_root->vs_common->pmm : NULL;
        if (!pmm_ptr) {
                pr_error("[nexus_delete_vspace] missing pmm\n");
                goto fail;
        }
        /* Get vspace node directly from vs (avoids O(log n) red-black tree
         * lookup) */
        struct nexus_node* vspace_node = (struct nexus_node*)vs->_vspace_node;
        struct nexus_node* vspace_page_manage_node =
                (struct nexus_node*)ROUND_DOWN((vaddr)vspace_node, PAGE_SIZE);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                goto fail;
        }
        /*first we need to unlink the vspace node (still need lock for tree
         * removal)*/
        lock_cas(&nexus_root->nexus_lock);
        nexus_rb_tree_vspace_remove(vspace_node,
                                    &(nexus_root->_vspace_rb_root));
        unlock_cas(&nexus_root->nexus_lock);

        lock_cas(&vspace_node->vs_common->nexus_vspace_lock);
        /*
                for release the vspace
                there's no need to use a tree to record one nexus node for
           release, which might lead to the stack overflow for a recursive
           algorithm so we must use a list entry to maintain the relationship,
                and the release the vspace is always using the
        */
        struct list_entry* curr = vspace_node->_vspace_list.next;
        struct list_entry* next;
        while (curr != &(vspace_node->_vspace_list)
               && curr != &(vspace_page_manage_node->_vspace_list)) {
                next = curr->next;
                struct nexus_node* node =
                        container_of(curr, struct nexus_node, _vspace_list);
                ppn_t ppn = have_mapped(
                        vs, VPN(node->addr), NULL, NULL, &percpu(Map_Handler));
                if (ppn < 0) {
                        pr_error("[ NEXUS ] ERROR: have_mapped error!\n");
                        unlock_cas(&vspace_node->vs_common->nexus_vspace_lock);
                        goto fail;
                }
                if (ppn != 0) {
                        unlink_rmap_list(pmm_ptr->zone, ppn, node);
                }

                ppn = unmap(vs, VPN(node->addr), 0, &percpu(Map_Handler));
                if (ppn < 0) {
                        /*
                         * Unmap failure means bad parameters or broken
                         * page-table structure (e.g. missing entry). Fix the
                         * bug in the caller or page-table maintenance; we are
                         * on an error path and cannot roll back already-freed
                         * entries.
                         */
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        unlock_cas(&vspace_node->vs_common->nexus_vspace_lock);
                        goto fail;
                }

                struct pmm* pmm_ptr = vspace_node->vs_common->pmm;
                error_t e = pmm_ptr->pmm_free(
                        pmm_ptr, ppn, nexus_node_get_pages(node));
                if (e) {
                        pr_error(
                                "[ Error ] pmm free error %d in free manage node with page %d\n",
                                e,
                                ppn);
                }
                list_del_init(curr);
                /*no need to maintain the rb tree*/
                nexus_free_entry(node, vspace_node);
                curr = next;
        }
        unlock_cas(&vspace_node->vs_common->nexus_vspace_lock);

        lock_cas(&nexus_root->nexus_lock);
        /*free the manage page*/
        free_manage_node_with_page(vspace_page_manage_node, nexus_root);
        unlock_cas(&nexus_root->nexus_lock);
        vs->_vspace_node = NULL;
        return;
fail:
        return;
}
static inline void insert_nexus_entry(struct nexus_node* free_nexus_entry,
                                      vaddr addr, bool is_2M,
                                      ENTRY_FLAGS_t flags, VS_Common* vs,
                                      struct nexus_node* vspace_root)
{
        if (!free_nexus_entry || !vs || !vspace_root) {
                return;
        }
        free_nexus_entry->addr = addr;
        free_nexus_entry->region_flags = flags;
        nexus_node_set_len(free_nexus_entry, is_2M);
        free_nexus_entry->vs_common = vs;
        list_add_head(&(free_nexus_entry->_vspace_list),
                      &(vspace_root->_vspace_list));
        nexus_rb_tree_insert(free_nexus_entry, &(vspace_root->_rb_root));
}
static inline void delete_nexus_entry(struct nexus_node* nexus_entry,
                                      struct nexus_node* vspace_root)
{
        if (!nexus_entry || !vspace_root) {
                return;
        }
        /*del from the vspace*/
        list_del_init(&(nexus_entry->_vspace_list));
        /*del from rb tree*/
        nexus_rb_tree_remove(nexus_entry, &vspace_root->_rb_root);
        nexus_free_entry(nexus_entry, vspace_root);
}

/*
 * @brief Clean up aux_list fields for all nodes in a temporary list
 *
 * @param temp_list: temporary list containing nodes to clean up
 * @param vspace_root: if non-NULL, also delete nodes from vspace (used on error
 * paths)
 *
 * @note Also cleans cache_data used for caching ppn/flags.
 *       Must set to 0 (not INIT_LIST_HEAD) because is_page_manage_node()
 *       checks for non-NULL pointers in manage_free_list (union alias).
 *
 * Used by functions that repurpose aux_list as temporary linkage.
 */
static inline void cleanup_aux_list(struct list_entry* temp_list,
                                    struct nexus_node* vspace_root)
{
        while (!list_empty(temp_list)) {
                struct list_entry* entry = temp_list->next;
                struct nexus_node* node =
                        container_of(entry, struct nexus_node, aux_list);
                list_del_init(&node->aux_list);
                /* Clean cached ppn/flags (must be 0 for is_page_manage_node
                 * check) */
                node->cache_data.cached_ppn = 0;
                node->cache_data.cached_flags = 0;
                if (vspace_root) {
                        delete_nexus_entry(node, vspace_root);
                }
        }
}
static struct nexus_node* _take_range(bool allow_2M, ENTRY_FLAGS_t eflags,
                                      struct nexus_node* vspace_node,
                                      vaddr free_page_addr, vaddr page_addr_end)
{
        if (!vspace_node) {
                return NULL;
        }
        /* Ensure "no overlap"*/
        if (page_addr_end <= free_page_addr)
                return NULL;
        if ((free_page_addr & (PAGE_SIZE - 1)) != 0
            || (page_addr_end & (PAGE_SIZE - 1)) != 0)
                return NULL;

        for (vaddr cur = free_page_addr; cur < page_addr_end;
             cur += PAGE_SIZE) {
                struct nexus_node* node =
                        nexus_rb_tree_search(&vspace_node->_rb_root, cur);
                if (node)
                        return NULL;
        }
        struct nexus_node* first_entry = NULL;
        struct list_entry inserted_list;
        INIT_LIST_HEAD(&inserted_list);
        for (; free_page_addr + PAGE_SIZE <= page_addr_end;
             free_page_addr += PAGE_SIZE) {
                while (allow_2M && ALIGNED(free_page_addr, MIDDLE_PAGE_SIZE)
                       && free_page_addr + MIDDLE_PAGE_SIZE <= page_addr_end) {
                        struct nexus_node* free_nexus_entry =
                                nexus_get_free_entry(vspace_node);
                        if (!free_nexus_entry) {
                                pr_error(
                                        "[ NEXUS ] cannot find a new free nexus entry\n");
                                goto fail;
                        }
                        insert_nexus_entry(free_nexus_entry,
                                           free_page_addr,
                                           true,
                                           eflags,
                                           vspace_node->vs_common,
                                           vspace_node);
                        list_add_head(&free_nexus_entry->aux_list,
                                      &inserted_list);
                        if (!first_entry) {
                                first_entry = free_nexus_entry;
                        }
                        free_page_addr += MIDDLE_PAGE_SIZE;
                }
                if (free_page_addr + PAGE_SIZE <= page_addr_end) {
                        struct nexus_node* free_nexus_entry =
                                nexus_get_free_entry(vspace_node);
                        if (!free_nexus_entry) {
                                pr_error(
                                        "[ NEXUS ] cannot find a new free nexus entry\n");
                                goto fail;
                        }
                        insert_nexus_entry(free_nexus_entry,
                                           free_page_addr,
                                           false,
                                           eflags,
                                           vspace_node->vs_common,
                                           vspace_node);
                        list_add_head(&free_nexus_entry->aux_list,
                                      &inserted_list);
                        if (!first_entry) {
                                first_entry = free_nexus_entry;
                        }
                }
        }
        /* detach temporary rollback links (success path - don't delete nodes)
         */
        cleanup_aux_list(&inserted_list, NULL);
        return first_entry;
fail:
        /* Clean up aux_list fields and delete nodes (error path) */
        cleanup_aux_list(&inserted_list, vspace_node);
        return NULL;
}

/* One 4K user page: private copy + dst map + nexus. Rollback on failure. */
static error_t _vspace_clone_copy_page(VS_Common* dst_vs,
                                       struct nexus_node* dst_nexus_node,
                                       VS_Common* src_vs, vaddr va,
                                       ENTRY_FLAGS_t src_flags, ppn_t src_ppn,
                                       struct map_handler* handler)
{
        size_t alloced = 0;
        ppn_t new_ppn = src_vs->pmm->pmm_alloc(src_vs->pmm, 1, &alloced);
        if (invalid_ppn(new_ppn) || alloced != 1)
                goto copy_error;
        if (map_handler_copy_page(handler, new_ppn, src_ppn) != REND_SUCCESS) {
                goto clean_page;
        }
        if (map(dst_vs, new_ppn, VPN(va), 3, src_flags, handler)
            != REND_SUCCESS) {
                goto clean_page;
        }
        if (!_take_range(false, src_flags, dst_nexus_node, va, va + PAGE_SIZE)) {
                goto clean_map;
        }
        return REND_SUCCESS;
clean_map:
        (void)unmap(dst_vs, VPN(va), 0, handler);
clean_page:
        src_vs->pmm->pmm_free(src_vs->pmm, new_ppn, 1);
copy_error:
        return -E_RENDEZVOS;
}

/* COW phase 1 only: dst map shared page + ref++ + dst nexus. Rollback on
 * failure. */
static error_t _vspace_clone_cow(VS_Common* dst_vs,
                                 struct nexus_node* dst_nexus_node,
                                 VS_Common* src_vs, vaddr va,
                                 ENTRY_FLAGS_t src_flags, ppn_t src_ppn,
                                 struct map_handler* handler)
{
        ENTRY_FLAGS_t ro_flags = clear_mask_u64(src_flags, PAGE_ENTRY_WRITE);
        ENTRY_FLAGS_t dst_flags = (src_flags & PAGE_ENTRY_WRITE) ? ro_flags :
                                                                   src_flags;

        if (map(dst_vs, src_ppn, VPN(va), 3, dst_flags, handler)
            != REND_SUCCESS)
                goto cow_error;
        if (pmm_change_pages_ref(src_vs->pmm, src_ppn, 1, true)
            != REND_SUCCESS) {
                goto clean_map;
        }
        if (!_take_range(false, dst_flags, dst_nexus_node, va, va + PAGE_SIZE)) {
                goto clean_ref;
        }
        return REND_SUCCESS;
clean_ref:
        (void)pmm_change_pages_ref(src_vs->pmm, src_ppn, 1, false);
clean_map:
        (void)unmap(dst_vs, VPN(va), 0, handler);
cow_error:
        return -E_RENDEZVOS;
}

static inline ENTRY_FLAGS_t
nexus_range_compute_flags(nexus_range_flags_mode_t mode,
                          ENTRY_FLAGS_t old_flags, ENTRY_FLAGS_t set_mask,
                          ENTRY_FLAGS_t clear_mask)
{
        switch (mode) {
        case NEXUS_RANGE_FLAGS_ABSOLUTE:
                return set_mask;
        case NEXUS_RANGE_FLAGS_DELTA:
                /* desired = (old | set_mask) & ~clear_mask */
                return clear_mask_u64(old_flags | set_mask, clear_mask);
        default:
                return set_mask;
        }
}

/*
 * Common core for batch flags update.
 *
 * - Caller holds vs->nexus_vspace_lock.
 * - update_list contains unique nodes to be updated, linked via aux_list.
 * - For each node:
 *   - cache_data.cached_ppn is valid ppn for node->addr in vs page tables.
 *   - cache_data.cached_flags is the original node->region_flags (for
 * rollback).
 *
 * On return, always detaches aux_list and clears cache_data for nodes in
 * update_list (via cleanup_aux_list).
 */
static error_t nexus_update_flags_list_core(
        VS_Common* vs, struct map_handler* handler, struct pmm* pmm_ptr,
        struct list_entry* update_list, nexus_range_flags_mode_t mode,
        ENTRY_FLAGS_t set_mask, ENTRY_FLAGS_t clear_mask)
{
        if (!vs || !handler || !pmm_ptr || !update_list)
                return -E_IN_PARAM;

        error_t ret = REND_SUCCESS;
        int updated_count = 0;

        /* Batch update page tables and nexus flags with full rollback */
        for (struct list_entry* entry = update_list->next; entry != update_list;
             entry = entry->next) {
                struct nexus_node* node =
                        container_of(entry, struct nexus_node, aux_list);

                /* Cached ppn from Phase 1 (stored in cache_data) */
                ppn_t ppn = node->cache_data.cached_ppn;
                ENTRY_FLAGS_t desired =
                        nexus_range_compute_flags(mode,
                                                  node->cache_data.cached_flags,
                                                  set_mask,
                                                  clear_mask);

                bool huge = (node->region_flags & PAGE_ENTRY_HUGE) != 0;
                if (huge) {
                        if (map(vs, ppn, VPN(node->addr), 2, desired, handler)
                            != REND_SUCCESS) {
                                ret = -E_RENDEZVOS;
                                goto rollback;
                        }
                        node->region_flags = desired | PAGE_ENTRY_HUGE;
                } else {
                        error_t e = nexus_update_node(
                                vs, handler, pmm_ptr, node, ppn, ppn, desired);
                        if (e != REND_SUCCESS) {
                                ret = e;
                                goto rollback;
                        }
                }
                updated_count++;
        }
        /* Success */
        cleanup_aux_list(update_list, NULL);
        return REND_SUCCESS;

rollback:
        /*
         * Full rollback: restore all successfully updated nodes to original
         * state. This ensures atomicity - either all nodes are updated or none
         * are.
         */
        int count = 0;
        for (struct list_entry* entry = update_list->next;
             entry != update_list && count < updated_count;
             entry = entry->next, count++) {
                struct nexus_node* node =
                        container_of(entry, struct nexus_node, aux_list);

                ENTRY_FLAGS_t old_flags = node->cache_data.cached_flags;
                bool huge = (old_flags & PAGE_ENTRY_HUGE) != 0;
                int level = huge ? 2 : 3;
                ppn_t ppn = node->cache_data.cached_ppn;

                /* Rollback page table */
                error_t e = map(
                        vs, ppn, VPN(node->addr), level, old_flags, handler);
                if (e != REND_SUCCESS) {
                        pr_error(
                                "[ NEXUS ] FATAL: rollback failed, system state inconsistent!\n");
                }
                /* Rollback nexus flags */
                node->region_flags = old_flags;
        }

        cleanup_aux_list(update_list, NULL);
        return ret;
}

/*
 * Apply a flags delta to all 4K user leaf mappings recorded in vspace nexus.
 *
 * desired = (old | set_mask) & ~clear_mask
 *
 * On failure, roll back already-updated nodes' PTE flags + region_flags.
 * Uses nexus_node::cache_data + aux_list as a temporary rollback chain.
 *
 * Caller must hold vs->nexus_vspace_lock.
 */
static error_t _vspace_update_user_leaf_flags(VS_Common* vs,
                                              struct nexus_node* vspace_node,
                                              ENTRY_FLAGS_t set_mask,
                                              ENTRY_FLAGS_t clear_mask,
                                              struct map_handler* handler)
{
        if (!vs || !vspace_node || !handler)
                return -E_IN_PARAM;
        if (!vs->pmm)
                return -E_IN_PARAM;

        struct list_entry update_list;
        INIT_LIST_HEAD(&update_list);

        for (struct list_entry* list_node = vspace_node->_vspace_list.next;
             list_node != &vspace_node->_vspace_list;
             list_node = list_node->next) {
                struct nexus_node* node = container_of(
                        list_node, struct nexus_node, _vspace_list);
                vaddr va = node->addr;
                if (va >= KERNEL_VIRT_OFFSET)
                        continue;
                if (node->region_flags & PAGE_ENTRY_HUGE)
                        goto cleanup_fail;
                if (!list_node_is_detached(&node->aux_list))
                        goto cleanup_fail;

                ENTRY_FLAGS_t old_flags = node->region_flags;
                ENTRY_FLAGS_t desired =
                        clear_mask_u64(old_flags | set_mask, clear_mask);
                if (desired == old_flags)
                        continue;

                ppn_t ppn = have_mapped(vs, VPN(va), NULL, NULL, handler);
                if (invalid_ppn(ppn) || ppn == 0)
                        goto cleanup_fail;

                node->cache_data.cached_flags = old_flags;
                node->cache_data.cached_ppn = 0;
                node->cache_data.cached_ppn = ppn;
                list_add_tail(&node->aux_list, &update_list);

                /* defer updates to common phase2 core */
        }

        return nexus_update_flags_list_core(vs,
                                            handler,
                                            vs->pmm,
                                            &update_list,
                                            NEXUS_RANGE_FLAGS_DELTA,
                                            set_mask,
                                            clear_mask);

cleanup_fail:
        cleanup_aux_list(&update_list, NULL);
        return -E_RENDEZVOS;
}

error_t vspace_clone(VS_Common* src_vs, VS_Common** dst_vs_out,
                     vspace_clone_flags_t flags, struct nexus_node* nexus_root)
{
        /*
         * All-or-nothing. Per-page undo: _vspace_clone_copy_page,
         * _vspace_clone_cow (COW phase 1 only). Earlier completed pages:
         * del_vspace. COW phase 2 (parent RO): _vspace_adjust_user_leaf_flags.
         * Extra PT levels from map(): reclaimed after nexus
         * tear unmaps leaves.
         */
        if (!src_vs || !dst_vs_out || !nexus_root)
                return -E_IN_PARAM;
        if (!vs_common_is_table_vspace(src_vs))
                return -E_IN_PARAM;
        if (!(flags & VSPACE_CLONE_F_USER_4K_ONLY))
                return -E_IN_PARAM;
        if (!!(flags & VSPACE_CLONE_F_COW_PREP)
            == !!(flags & VSPACE_CLONE_F_COPY_PAGES)) {
                /* must select exactly one strategy */
                return -E_IN_PARAM;
        }

        struct map_handler* handler = &percpu(Map_Handler);
        VS_Common* dst_vs = NULL;
        paddr dst_root = 0;
        struct nexus_node* dst_nexus_node = NULL;
        error_t ret = REND_SUCCESS;

        dst_vs = new_vspace();
        if (!dst_vs)
                return -E_RENDEZVOS;

        /* Start from kernel mappings only; user part is rebuilt from nexus. */
        dst_root = new_vs_root(0, handler);
        if (!dst_root) {
                ret = -E_RENDEZVOS;
                goto out_free_vspace;
        }
        set_vspace_root_addr(dst_vs, dst_root);

        dst_vs->pmm = src_vs->pmm;
        dst_nexus_node = nexus_create_vspace_root_node(nexus_root, dst_vs);
        if (!dst_nexus_node) {
                ret = -E_RENDEZVOS;
                goto out_free_vspace;
        }
        init_vspace(dst_vs, dst_vs->vspace_id, dst_nexus_node);

        struct nexus_node* src_vspace_node =
                (struct nexus_node*)src_vs->_vspace_node;
        if (!src_vspace_node || src_vspace_node->vs_common != src_vs) {
                ret = -E_RENDEZVOS;
                goto out_free_vspace;
        }

        lock_cas(&src_vs->nexus_vspace_lock);

        if (flags & VSPACE_CLONE_F_COPY_PAGES) {
                for (struct list_entry* list_node =
                             src_vspace_node->_vspace_list.next;
                     list_node != &src_vspace_node->_vspace_list;
                     list_node = list_node->next) {
                        struct nexus_node* node = container_of(
                                list_node, struct nexus_node, _vspace_list);
                        vaddr va = node->addr;

                        if (va >= KERNEL_VIRT_OFFSET)
                                continue;
                        if (node->region_flags & PAGE_ENTRY_HUGE) {
                                /*we only clone user part, no huge support now*/
                                ret = -E_IN_PARAM;
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                        ENTRY_FLAGS_t src_flags = node->region_flags;
                        ppn_t src_ppn = have_mapped(
                                src_vs, VPN(va), NULL, NULL, handler);
                        if (invalid_ppn(src_ppn)) {
                                ret = -E_RENDEZVOS;
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                        ret = _vspace_clone_copy_page(dst_vs,
                                                      dst_nexus_node,
                                                      src_vs,
                                                      va,
                                                      src_flags,
                                                      src_ppn,
                                                      handler);
                        if (ret != REND_SUCCESS) {
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                }
        } else {
                for (struct list_entry* list_node =
                             src_vspace_node->_vspace_list.next;
                     list_node != &src_vspace_node->_vspace_list;
                     list_node = list_node->next) {
                        struct nexus_node* node = container_of(
                                list_node, struct nexus_node, _vspace_list);
                        vaddr va = node->addr;

                        if (va >= KERNEL_VIRT_OFFSET)
                                continue;
                        if (node->region_flags & PAGE_ENTRY_HUGE) {
                                ret = -E_IN_PARAM;
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                        ENTRY_FLAGS_t src_flags = node->region_flags;
                        ppn_t src_ppn = have_mapped(
                                src_vs, VPN(va), NULL, NULL, handler);
                        if (invalid_ppn(src_ppn)) {
                                ret = -E_RENDEZVOS;
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                        ret = _vspace_clone_cow(dst_vs,
                                                dst_nexus_node,
                                                src_vs,
                                                va,
                                                src_flags,
                                                src_ppn,
                                                handler);
                        if (ret != REND_SUCCESS) {
                                unlock_cas(&src_vs->nexus_vspace_lock);
                                goto out_free_vspace;
                        }
                }

                /* Phase 2: parent set read only. */
                ret = _vspace_update_user_leaf_flags(
                        src_vs, src_vspace_node, 0, PAGE_ENTRY_WRITE, handler);
                if (ret != REND_SUCCESS) {
                        unlock_cas(&src_vs->nexus_vspace_lock);
                        goto out_free_vspace;
                }
        }
        unlock_cas(&src_vs->nexus_vspace_lock);

        *dst_vs_out = dst_vs;
        return REND_SUCCESS;
out_free_vspace:
        if (dst_vs) {
                del_vspace(&dst_vs);
        }
        return ret;
}

static void* _kernel_get_free_page(size_t page_num,
                                   struct nexus_node* nexus_root)
{
        if (!nexus_root) {
                pr_error("[ NEXUS ] _kernel_get_free_page: nexus_root NULL\n");
                return NULL;
        }
        if (!nexus_root->vs_common) {
                pr_error(
                        "[ NEXUS ] _kernel_get_free_page: no VSpace (vs union)\n");
                return NULL;
        }
        VS_Common* heap_ref = nexus_root_heap_ref(nexus_root);
        if (!heap_ref) {
                pr_error("[ NEXUS ] _kernel_get_free_page: heap ref NULL\n");
                return NULL;
        }
        vaddr free_page_addr, page_addr_end;
        struct nexus_node* first_entry = NULL;
        size_t alloced_page_number;
        lock_cas(&heap_ref->nexus_vspace_lock);
        VS_Common* vs = nexus_node_vspace(nexus_root);
        ENTRY_FLAGS_t kernel_eflags = PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                      | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE;

        /*get phy pages from pmm*/
        struct pmm* pmm_ptr =
                nexus_root->vs_common ? nexus_root->vs_common->pmm : NULL;
        if (!pmm_ptr) {
                pr_error("[ NEXUS ] _kernel_get_free_page: missing pmm\n");
                goto alloc_ppn_fail;
        }
        ppn_t ppn = pmm_ptr->pmm_alloc(pmm_ptr, page_num, &alloced_page_number);
        if (invalid_ppn(ppn) || alloced_page_number < page_num) {
                pr_error("[ NEXUS ] ERROR: init error allocated %lx\n",
                         alloced_page_number);
                goto alloc_ppn_fail;
        } else if (alloced_page_number > page_num) {
                /*
                        if allocated page number is unequal to the page number
                        then the upper level cannot get the allocated page
                   number info and it will not try to free the last pages then
                   those pages will not usable forever
                */
                pr_error(
                        "[ NEXUS ] ERROR: allocated pages is larger then needed pages\n");
                pr_error(
                        "[ NEXUS ] HINT: try to alloc %d pages, allocated %d pages\n",
                        page_num,
                        alloced_page_number);
                goto take_range_fail;
        }
        free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        page_addr_end = free_page_addr + page_num * PAGE_SIZE;

        first_entry = _take_range(
                true, kernel_eflags, nexus_root, free_page_addr, page_addr_end);
        if (!first_entry) {
                goto take_range_fail;
        }
        struct nexus_node* chain_start = first_entry;

        while (first_entry) {
                if (first_entry->region_flags & PAGE_ENTRY_HUGE) {
                        error_t map_res =
                                map(vs,
                                    PPN(KERNEL_VIRT_TO_PHY(first_entry->addr)),
                                    VPN(first_entry->addr),
                                    2,
                                    kernel_eflags,
                                    &percpu(Map_Handler));
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: kernel get free page map error 2M\n");
                                goto map_fail;
                        }
                        page_num -= MIDDLE_PAGES;
                } else {
                        error_t map_res =
                                map(vs,
                                    PPN(KERNEL_VIRT_TO_PHY(first_entry->addr)),
                                    VPN(first_entry->addr),
                                    3,
                                    kernel_eflags,
                                    &percpu(Map_Handler));
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: kernel get free page map error\n");
                                goto map_fail;
                        }
                        page_num--;
                }
                link_rmap_list(pmm_ptr->zone,
                               PPN(KERNEL_VIRT_TO_PHY(first_entry->addr)),
                               first_entry);
                if (page_num <= 0) {
                        break;
                }
                first_entry = nexus_rb_tree_next(first_entry);
        }

        unlock_cas(&heap_ref->nexus_vspace_lock);
        return (void*)free_page_addr;
map_fail:
        /*
         * Full rollback: unmap any already-mapped entries (only unmap, do not
         * delete yet), then delete all entries in our range. The tree is keyed
         * by addr; nexus_rb_tree_next gives in-order successor in the whole
         * tree, so we must identify our entries by [free_page_addr,
         * page_addr_end).
         */
        struct nexus_node* node = chain_start;
        struct nexus_node* next;
        /* First loop: unmap only the entries we had already mapped (addr <
         * first_entry) */
        while (node && node->addr >= free_page_addr
               && node->addr < page_addr_end
               && node->addr < first_entry->addr) {
                unlink_rmap_list(pmm_ptr->zone,
                                 PPN(KERNEL_VIRT_TO_PHY(node->addr)),
                                 node);
                ppn_t unmap_ppn =
                        unmap(vs, VPN(node->addr), 0, &percpu(Map_Handler));
                (void)unmap_ppn;
                node = nexus_rb_tree_next(node);
        }
        /* Second loop: delete all entries in our range; restart from
         * chain_start */
        node = chain_start;
        while (node && node->addr >= free_page_addr
               && node->addr < page_addr_end) {
                next = nexus_rb_tree_next(node);
                delete_nexus_entry(node, nexus_root);
                node = next;
        }
take_range_fail:
        /* _take_range failed and rolled back; only return ppn */
        pmm_ptr->pmm_free(pmm_ptr, ppn, alloced_page_number);
alloc_ppn_fail:
        unlock_cas(&heap_ref->nexus_vspace_lock);
        return NULL;
}
/*
        we try to get the user physical pages and map the physical pages to the
   virtual pages which only based on the 4K pages in user space
*/
error_t user_fill_range(struct nexus_node* first_entry, int page_num,
                        struct nexus_node* vspace_node)
{
        size_t alloced_page_number;
        vaddr free_page_addr, page_addr_end;
        struct nexus_node* chain_start = first_entry;
        ppn_t current_ppn = -E_RENDEZVOS; /* ppn to free on map failure */
        struct pmm* pmm_ptr;

        if (!vspace_node || !vspace_node->vs_common
            || !vspace_node->vs_common->pmm || !first_entry)
                return -E_RENDEZVOS;

        free_page_addr = first_entry->addr;
        page_addr_end = first_entry->addr + page_num * PAGE_SIZE;
        pmm_ptr = vspace_node->vs_common->pmm;

        VS_Common* vs = vspace_node->vs_common;
        lock_cas(&vs->nexus_vspace_lock);
        while (first_entry) {
                ppn_t ppn = have_mapped(vs,
                                        VPN(first_entry->addr),
                                        NULL,
                                        NULL,
                                        &percpu(Map_Handler));
                if (!invalid_ppn(ppn)) {
                        goto handle_next_page;
                }
                ppn = pmm_ptr->pmm_alloc(pmm_ptr, 1, &alloced_page_number);
                if (invalid_ppn(ppn) || alloced_page_number != 1) {
                        pr_error("[ NEXUS ] ERROR: init error allocated %lx\n",
                                 alloced_page_number);
                        goto fail;
                }
                current_ppn = ppn;
                error_t map_res = map(vs,
                                      ppn,
                                      VPN(first_entry->addr),
                                      3,
                                      first_entry->region_flags,
                                      &percpu(Map_Handler));
                if (map_res) {
                        pr_error(
                                "[ NEXUS ] ERROR: user get free page map error\n");
                        goto fail;
                }
                current_ppn = -E_RENDEZVOS; /* committed */
        handle_next_page:
                link_rmap_list(pmm_ptr->zone, ppn, first_entry);
                page_num--;
                if (page_num <= 0)
                        break;
                first_entry = nexus_rb_tree_next(first_entry);
        }

        unlock_cas(&vs->nexus_vspace_lock);
        return REND_SUCCESS;
fail:
        /*
         * Full rollback: unmap already-mapped entries (by range), free
         * current_ppn if we failed at map, then delete all entries in range.
         */
        struct nexus_node* node = chain_start;
        struct nexus_node* next;
        while (node && node->addr >= free_page_addr
               && node->addr < page_addr_end
               && node->addr < first_entry->addr) {
                ppn_t up = unmap(vs, VPN(node->addr), 0, &percpu(Map_Handler));
                if (!invalid_ppn(up)) {
                        list_del_init(&node->rmap_list);
                        pmm_ptr->pmm_free(pmm_ptr, up, 1);
                }
                node = nexus_rb_tree_next(node);
        }
        if (!invalid_ppn(current_ppn)) {
                pmm_ptr->pmm_free(pmm_ptr, current_ppn, 1);
        }
        node = chain_start;
        while (node && node->addr >= free_page_addr
               && node->addr < page_addr_end) {
                next = nexus_rb_tree_next(node);
                delete_nexus_entry(node, vspace_node);
                node = next;
        }
        /* We only hold the lock when we entered the while (goto fail from loop)
         */
        unlock_cas(&vs->nexus_vspace_lock);
        return -E_RENDEZVOS;
}
static struct nexus_node* _user_take_range(int page_num, vaddr target_vaddr,
                                           struct nexus_node* vspace_node,
                                           ENTRY_FLAGS_t user_eflags)
{
        vaddr free_page_addr, page_addr_end;
        struct nexus_node* first_entry = NULL;
        /*and obviously, the address 0 should not accessed by any of the
         * user*/
        free_page_addr = (((u64)target_vaddr) >> 12) << 12;
        if (free_page_addr != target_vaddr || !vspace_node) {
                return NULL;
        }

        lock_cas(&vspace_node->vs_common->nexus_vspace_lock);
        /*adjust the flags, the user flags must not include PAGE_ENTRY_GLOBAL*/
        user_eflags = clear_mask_u64(user_eflags, PAGE_ENTRY_GLOBAL);

        page_addr_end = free_page_addr + page_num * PAGE_SIZE;

        first_entry = _take_range(
                false, user_eflags, vspace_node, free_page_addr, page_addr_end);

        unlock_cas(&vspace_node->vs_common->nexus_vspace_lock);
        return first_entry;
}
static error_t _unfill_range(void* p, int page_num, VS_Common* vs,
                             struct nexus_node* vspace_node,
                             struct nexus_node* node)
{
        if (!p || !vs || !vspace_node || !node) {
                return -E_IN_PARAM;
        }
        error_t e = REND_SUCCESS;
        vaddr free_end = (vaddr)p + page_num * PAGE_SIZE;
        while (node) {
                bool need_break = false;
                if (node->addr + nexus_node_get_len(node) >= free_end) {
                        need_break = true;
                }
                if (nexus_node_get_len(node) == MIDDLE_PAGE_SIZE
                    && node->addr < free_end
                    && node->addr + nexus_node_get_len(node) > free_end) {
                        pr_error(
                                "[ NEXUS ] ERROR: split 2M page and we truncate it");
                        break;
                }
                ppn_t ppn = unmap(vs, VPN(node->addr), 0, &percpu(Map_Handler));
                if (ppn < 0) {
                        /*
                         * Unmap failure here means bad parameters or broken
                         * page-table structure (e.g. missing entry). That is a
                         * bug to fix in the caller or page-table maintenance,
                         * not a rollback scenario: we are already on an error
                         * path and cannot meaningfully "roll back" unfill.
                         */
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        return -E_RENDEZVOS;
                }
                struct pmm* pmm_ptr = vspace_node->vs_common->pmm;
                list_del_init(&node->rmap_list);
                e = pmm_ptr->pmm_free(pmm_ptr, ppn, nexus_node_get_pages(node));
                if (e) {
                        pr_error(
                                "[ Error ] unfill range pmm free error %d in free manage node with page %d\n",
                                e,
                                ppn);
                }
                node = nexus_rb_tree_next(node);
                if (!node || is_page_manage_node(node)) {
                        need_break = true;
                }
                if (need_break)
                        break;
        }
        return e;
}
static error_t _kernel_free_pages(void* p, int page_num,
                                  struct nexus_node* nexus_root)
{
        if (!nexus_root) {
                return -E_IN_PARAM;
        }
        vaddr free_end = (vaddr)p + page_num * PAGE_SIZE;
        /*in kernel alloc, only alloced one time but might mapped
         * several times*/
        VS_Common* heap_ref = nexus_root_heap_ref(nexus_root);
        if (!heap_ref) {
                pr_error("[ NEXUS ] _kernel_free_pages: heap ref NULL\n");
                return -E_IN_PARAM;
        }
        lock_cas(&heap_ref->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&nexus_root->_rb_root, (vaddr)p);
        if (!node) {
                /*
                        TODO: if cannot find the nexus node
                        which might caused by this page is under another core's
                   nexus
                */
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%lx 0x%lx\n",
                        (vaddr)p,
                        (vaddr)nexus_root);
                unlock_cas(&heap_ref->nexus_vspace_lock);
                return -E_IN_PARAM;
        }

        VS_Common* vs = nexus_node_vspace(nexus_root);
        error_t res = _unfill_range(p, page_num, vs, nexus_root, node);
        if (res) {
                unlock_cas(&heap_ref->nexus_vspace_lock);
                return -E_RENDEZVOS;
        }
        /*then delete the nexus_entry*/
        while (node) {
                bool need_break = false;
                if (node->addr + nexus_node_get_len(node) >= free_end) {
                        need_break = true;
                }
                if (nexus_node_get_len(node) == MIDDLE_PAGE_SIZE
                    && node->addr < free_end
                    && node->addr + nexus_node_get_len(node) > free_end) {
                        pr_error(
                                "[ NEXUS ] ERROR: split 2M page and we truncate it");
                        break;
                }
                struct nexus_node* old_node = node;
                node = nexus_rb_tree_next(node);
                if (!node || is_page_manage_node(node)) {
                        need_break = true;
                }
                delete_nexus_entry(old_node, nexus_root);
                /*
                        the delete nexus entry function
                        might free the page,
                        and the next node might be the freed page manage node
                        so we have to use the curr node to judge only
                */
                if (need_break)
                        break;
        }
        unlock_cas(&heap_ref->nexus_vspace_lock);
        return REND_SUCCESS;
}
/*
        we try to unmap and free the physical pages but do not free the virtual
   ranges which only based on the 4K pages in user space remember that it must
   unmap first and then free it
*/
error_t user_unfill_range(void* p, int page_num, VS_Common* vs,
                          struct nexus_node* vspace_node)
{
        lock_cas(&vs->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, (vaddr)p);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%lx 0x%lx\n",
                        (vaddr)p,
                        (vaddr)vspace_node);
                unlock_cas(&vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }
        error_t res = _unfill_range(p, page_num, vs, vspace_node, node);
        unlock_cas(&vs->nexus_vspace_lock);
        return res;
}

static error_t _user_release_range(void* p, int page_num, VS_Common* vs,
                                   struct nexus_node* vspace_node)
{
        vaddr free_end = (vaddr)p + page_num * PAGE_SIZE;

        lock_cas(&vs->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, (vaddr)p);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%lx 0x%lx\n",
                        (vaddr)p,
                        (vaddr)vspace_node);
                unlock_cas(&vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }
        while (node) {
                bool need_break = false;
                if (node->addr + nexus_node_get_len(node) >= free_end) {
                        need_break = true;
                }
                if (nexus_node_get_len(node) == MIDDLE_PAGE_SIZE
                    && node->addr < free_end
                    && node->addr + nexus_node_get_len(node) > free_end) {
                        pr_error(
                                "[ NEXUS ] ERROR: split 2M page and we truncate it");
                        break;
                }
                struct nexus_node* old_node = node;
                node = nexus_rb_tree_next(node);
                if (!node || is_page_manage_node(node)) {
                        need_break = true;
                }
                delete_nexus_entry(old_node, vspace_node);
                /*
                        the delete nexus entry function
                        might free the page,
                        and the next node might be the freed page manage node
                        so we have to use the curr node to judge only
                */
                if (need_break)
                        break;
        }
        unlock_cas(&vs->nexus_vspace_lock);
        return REND_SUCCESS;
}
void* get_free_page(size_t page_num, vaddr target_vaddr,
                    struct nexus_node* nexus_root, VS_Common* vs,
                    ENTRY_FLAGS_t flags)
{
        /*first check the input parameter*/
        if (page_num == 0 || !nexus_root) {
                pr_error("[ NEXUS ] error input parameter\n");
                return NULL;
        }
        void* res = NULL;
        if (target_vaddr >= KERNEL_VIRT_OFFSET) {
                res = _kernel_get_free_page(page_num, nexus_root);
        } else {
                /*user space get free page must have a vs*/
                if (!vs) {
                        pr_error("[ NEXUS ] we must have a vspace here\n");
                        return NULL;
                }
                /* Get vspace node directly from vs (avoids O(log n) lookup +
                 * lock) */
                struct nexus_node* vspace_node =
                        (struct nexus_node*)vs->_vspace_node;
                if (!vspace_node || vspace_node->vs_common != vs) {
                        pr_error(
                                "[Error] no such a vspace in nexus or vs common is not equal\n");
                        return NULL;
                }
                struct nexus_node* first_entry = _user_take_range(
                        page_num, target_vaddr, vspace_node, flags);
                if (first_entry
                    && !user_fill_range(first_entry, page_num, vspace_node)) {
                        res = (void*)(first_entry->addr);
                }
        }
        return res;
}
/*
        for the free pages function,
        we just free the page start from p and end at p+page_num*PAGE_SIZE,
        for example, we have 3 regions:
        [0x1000,0x3000),[0x5000,0x6000),[0x7000,0x9000),
        and with a p = 0x2000,page_num = 6,
        which will clean the region from 0x2000-0x8000
        only 2 regions left:
        [0x1000,0x2000),[0x8000,0x9000)
        although there's 2 hole in those 3 regions
*/
error_t free_pages(void* p, int page_num, VS_Common* vs,
                   struct nexus_node* nexus_root)
{
        if (!p || !nexus_root || (((vaddr)p) & 0xfff)) {
                pr_error("[ ERROR ] ERROR: error input arg\n");
                return -E_IN_PARAM;
        }
        error_t res = 0;
        if ((vaddr)p >= KERNEL_VIRT_OFFSET) {
                res = _kernel_free_pages(p, page_num, nexus_root);
        } else {
                /*user space free pages must have a vs*/
                if (!vs) {
                        pr_error("[ NEXUS ] we must have a vspace here\n");
                        return -E_IN_PARAM;
                }
                /* Get vspace node directly from vs (avoids O(log n) lookup +
                 * lock) */
                struct nexus_node* vspace_node =
                        (struct nexus_node*)vs->_vspace_node;
                if (!vspace_node || vspace_node->vs_common != vs) {
                        pr_error(
                                "[ ERROR ] ERROR:no vspace node or vspace's node error!\n");
                        return -E_RENDEZVOS;
                }
                res = user_unfill_range(p, page_num, vs, vspace_node);
                if (!res)
                        res = _user_release_range(p, page_num, vs, vspace_node);
        }
        return res;
}

error_t nexus_update_range_flags(struct nexus_node* nexus_root, VS_Common* vs,
                                 vaddr start_addr, u64 length,
                                 nexus_range_flags_mode_t mode,
                                 ENTRY_FLAGS_t set_mask,
                                 ENTRY_FLAGS_t clear_mask)
{
        if (!nexus_root || !vs || length == 0)
                return -E_IN_PARAM;
        if ((start_addr & (PAGE_SIZE - 1)) != 0)
                return -E_IN_PARAM;

        // vs type validation: only support user table vspace
        if (!vs_common_is_table_vspace(vs))
                return -E_IN_PARAM;

        // Length and overflow validation
        u64 len_aligned = ROUND_UP(length, PAGE_SIZE);
        vaddr end_addr = start_addr + (vaddr)len_aligned;
        if (end_addr < start_addr)
                return -E_IN_PARAM;

        // Explicitly reject kernel space - this function is for user space only
        if (start_addr >= KERNEL_VIRT_OFFSET || end_addr >= KERNEL_VIRT_OFFSET)
                return -E_IN_PARAM;

        // Get vspace node directly from vs (avoids O(log n) red-black tree
        // lookup)
        struct nexus_node* vspace_node = (struct nexus_node*)vs->_vspace_node;
        if (!vspace_node || vspace_node->vs_common != vs)
                return -E_RENDEZVOS;

        lock_cas(&vs->nexus_vspace_lock);
        struct map_handler* handler = &percpu(Map_Handler);
        struct pmm* pmm_ptr = vs->pmm;

        /*
         * Two-phase algorithm for atomic batch update with full rollback:
         * Phase 1: Collection + Validation - collect and validate nodes in one
         * pass Phase 2: Update with rollback - batch update with full rollback
         * on failure
         *
         * Field repurposing (safe due to vspace lock):
         * - aux_list: temporary linked list for batch processing
         * - cache_data.cached_ppn: cached ppn (avoids repeated page table
         * lookup)
         * - cache_data.cached_flags: cached original flags (for rollback)
         *
         * SAFETY: The vspace lock ensures no concurrent is_page_manage_node()
         * checks will misinterpret cache_data as manage_free_list. All fields
         * are restored to NULL before lock release.
         *
         * Atomicity guarantee: either all nodes are updated successfully, or
         * all nodes are restored to their original state.
         */
        struct list_entry update_list;
        struct nexus_node* node;
        error_t ret = REND_SUCCESS;
        INIT_LIST_HEAD(&update_list);

        // Phase 1: Collect unique nodes and validate immediately on first
        // encounter
        for (vaddr cur = start_addr; cur < end_addr;) {
                node = nexus_rb_tree_search(&vspace_node->_rb_root, cur);

                if (!node || cur < node->addr
                    || cur >= node->addr + nexus_node_get_len(node)) {
                        ret = -E_IN_PARAM;
                        goto cleanup;
                }

                /* Add to update list if not already present. In-use nodes are
                 * detached. */
                if (list_node_is_detached(&node->aux_list)) {
                        ppn_t ppn = have_mapped(
                                vs, VPN(node->addr), NULL, NULL, handler);
                        if (invalid_ppn(ppn)) {
                                ret = -E_RENDEZVOS;
                                goto cleanup;
                        }
                        /* Cache ppn and original flags for potential rollback
                         */
                        node->cache_data.cached_ppn = ppn;
                        node->cache_data.cached_flags = node->region_flags;
                        list_add_head(&node->aux_list, &update_list);
                }

                // Skip to next node boundary (handles large pages efficiently)
                vaddr node_end = node->addr + nexus_node_get_len(node);
                cur = (node_end < end_addr) ? node_end : end_addr;
        }

        /*
         * Phase 2: update + rollback core. Also cleans aux_list/cache_data for
         * all nodes in update_list on all paths.
         */
        ret = nexus_update_flags_list_core(
                vs, handler, pmm_ptr, &update_list, mode, set_mask, clear_mask);

cleanup:
        /* phase2 core already cleaned update_list; safe to call again. */
        cleanup_aux_list(&update_list, NULL);
        unlock_cas(&vs->nexus_vspace_lock);
        return ret;
}

error_t nexus_remap_user_leaf(VS_Common* vs, vaddr va, ppn_t new_ppn,
                              ENTRY_FLAGS_t new_flags, ppn_t expect_old_ppn)
{
        if (!vs || (va & (PAGE_SIZE - 1)))
                return -E_IN_PARAM;
        if (!vs_common_is_table_vspace(vs))
                return -E_IN_PARAM;
        if (invalid_ppn(new_ppn))
                return -E_IN_PARAM;
        if (va >= KERNEL_VIRT_OFFSET)
                return -E_IN_PARAM;
        if (new_flags & PAGE_ENTRY_HUGE)
                return -E_IN_PARAM;

        struct nexus_node* vspace_node = (struct nexus_node*)vs->_vspace_node;
        if (!vspace_node || vspace_node->vs_common != vs)
                return -E_RENDEZVOS;
        struct pmm* pmm_ptr = vs->pmm;
        if (!pmm_ptr)
                return -E_RENDEZVOS;

        struct map_handler* handler = &percpu(Map_Handler);
        lock_cas(&vs->nexus_vspace_lock);

        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, va);
        if (!node || node->addr != va
            || (node->region_flags & PAGE_ENTRY_HUGE)) {
                unlock_cas(&vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }

        ppn_t old_ppn = have_mapped(vs, VPN(va), NULL, NULL, handler);
        if (invalid_ppn(old_ppn)) {
                unlock_cas(&vs->nexus_vspace_lock);
                return -E_RENDEZVOS;
        }
        if (!invalid_ppn(expect_old_ppn) && old_ppn != expect_old_ppn) {
                unlock_cas(&vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }

        error_t e = nexus_update_node(
                vs, handler, pmm_ptr, node, old_ppn, new_ppn, new_flags);
        unlock_cas(&vs->nexus_vspace_lock);
        return e;
}

error_t unfill_phy_page(MemZone* zone, ppn_t ppn, u64 new_entry_addr)
{
        ZonePageCursor cur;
        Page* p_ptr = zone_page_cursor_init(&cur, zone, ppn);
        if (!p_ptr) {
                pr_error("[ NEXUS ] cannot find phy page with ppn %lx\n", ppn);
                return -E_RENDEZVOS;
        }
        struct nexus_node* node = NULL;
        const char* relink_err = NULL;
        /*
         * One rmap node at a time: under pmm lock, detach one entry from
         * Page.rmap_list, then drop pmm before unmap (which takes nexus/vspace
         * locks). Avoids nexus<->pmm lock order inversion vs link_rmap_list;
         * no fixed snapshot cap.
         */
        for (;;) {
                pmm_zone_lock(zone);
                if (p_ptr->rmap_list.next == &p_ptr->rmap_list) {
                        pmm_zone_unlock(zone);
                        break;
                }
                node = container_of(
                        p_ptr->rmap_list.next, struct nexus_node, rmap_list);
                list_del_init(&node->rmap_list);
                pmm_zone_unlock(zone);

                VS_Common* vs = nexus_node_vspace(node);
                if (!vs) {
                        relink_err =
                                "[ NEXUS ] unfill_phy_page: nexus_node_vspace NULL\n";
                        goto relink_rmap_fail;
                }
                struct nexus_node* vspace_node =
                        (struct nexus_node*)(node->vs_common->_vspace_node);
                if (!vspace_node || !vspace_node->vs_common
                    || !vspace_node->vs_common->pmm) {
                        relink_err =
                                "[ NEXUS ] unfill_phy_page: missing vspace_node/pmm\n";
                        goto relink_rmap_fail;
                }
                ppn_t unmap_ppn = unmap(vs,
                                        VPN(node->addr),
                                        new_entry_addr,
                                        &percpu(Map_Handler));
                if (ppn != unmap_ppn) {
                        pr_error(
                                "[ NEXUS ] try to unmap shared page but return ppn is not equal\n");
                        return -E_RENDEZVOS;
                }
        }
        /*we only need to free once but need to change the ref count to 1*/
        p_ptr->ref_count = 1;
        struct pmm* pmm_ptr = p_ptr->sec->zone->pmm;
        error_t e = pmm_ptr->pmm_free(pmm_ptr, ppn, 1);
        if (e) {
                pr_error(
                        "[ Error ] unfill phy page pmm free error %d in free manage node with page %d\n",
                        e,
                        ppn);
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;

relink_rmap_fail:
        pmm_zone_lock(zone);
        list_add_head(&node->rmap_list, &p_ptr->rmap_list);
        pmm_zone_unlock(zone);
        pr_error("%s", relink_err);
        return -E_RENDEZVOS;
}

int nexus_kernel_page_owner_cpu(vaddr kva)
{
        if (kva < KERNEL_VIRT_OFFSET)
                return INVALID_CPU_ID;
        paddr pa = KERNEL_VIRT_TO_PHY(kva);
        ppn_t ppn = PPN(pa);
        if (invalid_ppn(ppn))
                return INVALID_CPU_ID;
        ZonePageCursor cur;
        Page* p_ptr = zone_page_cursor_init(&cur, &mem_zones[ZONE_NORMAL], ppn);
        if (!p_ptr)
                return INVALID_CPU_ID;
        /*
         * Whole-page kmem routing uses only kernel-heap nexus nodes: `cpu_id`
         * on KERNEL_HEAP_REF (per-CPU nexus_kernel_heap_vs_common). The same
         * PPN may also appear on rmap via user mappings; those must not define
         * the kmem owner (not 1:1 with kmem, list order is not authoritative).
         */
        MemZone* zone = p_ptr->sec->zone;
        int out = INVALID_CPU_ID;
        pmm_zone_lock(zone);
        struct list_entry* list = p_ptr->rmap_list.next;
        while (list != &p_ptr->rmap_list) {
                struct nexus_node* node =
                        container_of(list, struct nexus_node, rmap_list);
                list = list->next;
                if (is_page_manage_node(node))
                        continue;
                u64 len = (node->region_flags & PAGE_ENTRY_HUGE) ?
                                  MIDDLE_PAGE_SIZE :
                                  PAGE_SIZE;
                if (kva < node->addr || kva >= node->addr + len)
                        continue;
                if (!vs_common_is_heap_ref(node->vs_common))
                        continue;
                {
                        int cpu = (int)node->vs_common->cpu_id;
                        if (cpu < 0 || cpu >= RENDEZVOS_MAX_CPU_NUMBER)
                                break;
                        out = cpu;
                        break;
                }
        }
        pmm_zone_unlock(zone);
        return out;
}
