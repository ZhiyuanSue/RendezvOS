#include <common/string.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/vmm.h>

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
        u64 key = vspace_node->vs->vspace_root_addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node = container_of(
                        parent, struct nexus_node, _vspace_rb_node);
                if (key < (u64)tmp_node->vs->vspace_root_addr)
                        new = &parent->left_child;
                else if (key > (u64)tmp_node->vs->vspace_root_addr)
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
                if (vspace_root_addr < tmp_node->vs->vspace_root_addr)
                        node = node->left_child;
                else if (vspace_root_addr > tmp_node->vs->vspace_root_addr)
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
        return tmp_node;
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
                                   struct nexus_node* nexus_root)
{
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;
        /*init the node 0 point to this page*/
        n_node->addr = vpage_addr;
        /*the manage page is promised in kernel space, and use a identity
         * mapping*/
        nexus_node_set_len(n_node, false);
        n_node->page_left_nexus = NEXUS_PER_PAGE - 1;
        /*init the list*/
        INIT_LIST_HEAD(&n_node->_free_list);
        for (int i = 1; i < NEXUS_PER_PAGE; i++) {
                list_add_head(&n_node[i]._free_list, &n_node->_free_list);
        }
        /*insert to rb tree*/
        list_add_head(&(n_node->_vspace_list), &(nexus_root->_vspace_list));
        nexus_rb_tree_insert(n_node, &nexus_root->_rb_root);
}
static struct nexus_node* init_vspace_nexus(vaddr nexus_page_addr, VSpace* vs,
                                            struct map_handler* handler,
                                            struct rb_root* _vspace_rb_root)
{
        if (!ALIGNED(nexus_page_addr, PAGE_SIZE)) {
                pr_error("[ NEXUS ] ERROR: init vspace nexus input error\n");
                return NULL;
        }
        /*remember clean this page*/
        memset((void*)nexus_page_addr, '\0', PAGE_SIZE);
        struct nexus_node* n_node = (struct nexus_node*)nexus_page_addr;

        /*init the node 1 as the root and let the nexus_root point to it*/
        struct nexus_node* root_node = &n_node[1];
        n_node[1].handler = handler;
        n_node[1].vs = vs;
        INIT_LIST_HEAD(&n_node[1].manage_free_list);
        INIT_LIST_HEAD(&n_node[1]._vspace_list);
        lock_init_cas(&n_node[1].nexus_lock);
        lock_init_cas(&n_node[1].vs->nexus_vspace_lock);
        if (_vspace_rb_root) {
                nexus_rb_tree_vspace_insert(&n_node[1], _vspace_rb_root);
        } else {
                nexus_rb_tree_vspace_insert(&n_node[1],
                                            &n_node[1]._vspace_rb_root);
        }

        nexus_init_manage_page(nexus_page_addr, &n_node[1]);
        /*you have to del the root node from the free list,for in init manage
         * page, it have been linked*/
        list_del_init(&n_node[1]._free_list);

        n_node->page_left_nexus -= 1;
        list_add_head(&n_node->manage_free_list, &root_node->manage_free_list);
        return &n_node[1];
}
/*return a nexus root node*/
struct nexus_node* init_nexus(struct map_handler* handler)
{
        VSpace* vs = current_vspace;
        /*get a phy page*/
        size_t alloced_page_number;
        i64 nexus_init_page =
                handler->pmm->pmm_alloc(1, ZONE_NORMAL, &alloced_page_number);
        if (nexus_init_page <= 0 || alloced_page_number != 1) {
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
                handler,
                NULL)) {
                pr_error("[ NEXUS ] ERROR: init nexus map error\n");
                return NULL;
        }
        return init_vspace_nexus(vpage_addr, vs, handler, NULL);
}
static struct nexus_node* nexus_get_free_entry(struct nexus_node* root_node)
{
        size_t alloced_page_number;
        /*from manage_free_list find one manage page that have free node*/
        struct list_entry* manage_free_list_node = &root_node->manage_free_list;
        struct list_entry* lp = manage_free_list_node->next;
        if (lp == manage_free_list_node) {
                /*means no free manage can use, try alloc a new one*/
                VSpace* vs = root_node->vs;

                lock_mcs(&root_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, root_node->handler->cpu_id));
                i64 nexus_new_page = root_node->handler->pmm->pmm_alloc(
                        1, ZONE_NORMAL, &alloced_page_number);
                unlock_mcs(&root_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, root_node->handler->cpu_id));
                if (nexus_new_page <= 0 || alloced_page_number != 1) {
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
                            root_node->handler,
                            &root_node->vs->vspace_lock);
                if (map_res) {
                        pr_error("[ NEXUS ] ERROR: get free entry map error\n");
                        return NULL;
                }
                memset((void*)vpage_addr, '\0', PAGE_SIZE);
                nexus_init_manage_page(vpage_addr, root_node);
                lp = &((struct nexus_node*)vpage_addr)->manage_free_list;
                /*insert the lp to the manage page list*/
                list_add_head(lp, &root_node->manage_free_list);
        }
        if (!lp || lp == manage_free_list_node) {
                pr_error("[ ERROR ]find an free manage page fail\n");
                return NULL;
        }
        /*here we promise we have a usable lp, then from lp get the manage page
         * mate info*/
        struct nexus_node* usable_manage_page =
                container_of(lp, struct nexus_node, manage_free_list);
        /*use the free list to find a new entry*/
        struct list_entry* usable_entry = usable_manage_page->_free_list.next;
        struct nexus_node* usable_manage_entry =
                container_of(usable_entry, struct nexus_node, _free_list);
        /*clean the entry,remember that we must del it first then clean the
         * entry*/
        list_del_init(usable_entry);
        usable_manage_page->page_left_nexus--;
        if (usable_manage_page->page_left_nexus == 0) {
                list_del_init(&usable_manage_page->manage_free_list);
        }
        memset(usable_manage_entry, '\0', sizeof(struct nexus_node));
        return usable_manage_entry;
}
static void free_manage_node_with_page(struct nexus_node* page_manage_node,
                                       struct nexus_node* vspace_root)
{
        /*free this manage page*/
        i64 ppn = unmap(vspace_root->vs,
                        VPN((vaddr)page_manage_node),
                        0,
                        vspace_root->handler,
                        &vspace_root->vs->vspace_lock);
        if (ppn < 0) {
                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                return;
        }
        lock_mcs(&vspace_root->handler->pmm->spin_ptr,
                 &per_cpu(pmm_spin_lock, vspace_root->handler->cpu_id));
        vspace_root->handler->pmm->pmm_free(ppn, 1);
        unlock_mcs(&vspace_root->handler->pmm->spin_ptr,
                   &per_cpu(pmm_spin_lock, vspace_root->handler->cpu_id));
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
        list_add_head(&nexus_entry->_free_list, &page_manage_node->_free_list);
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
                                                 VSpace* vs)
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
        i64 nexus_init_page = nexus_root->handler->pmm->pmm_alloc(
                1, ZONE_NORMAL, &alloced_page_number);
        if (nexus_init_page <= 0 || alloced_page_number != 1) {
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
                nexus_root->handler,
                &nexus_root->vs->vspace_lock)) {
                pr_error("[ NEXUS ] ERROR: init nexus map error\n");
                goto fail;
        }
        res = init_vspace_nexus(vpage_addr,
                                vs,
                                nexus_root->handler,
                                &nexus_root->_vspace_rb_root);
        unlock_cas(&nexus_root->nexus_lock);
fail:
        return res;
}

void nexus_migrate_vspace(struct nexus_node* src_nexus_root,
                          struct nexus_node* dst_nexus_root, VSpace* vs)
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
        vspace_node->handler = dst_nexus_root->handler;
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

void nexus_delete_vspace(struct nexus_node* nexus_root, VSpace* vs)
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
        /*try to find the vs paddr root ,if not, error*/
        lock_cas(&nexus_root->nexus_lock);
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                unlock_cas(&nexus_root->nexus_lock);
                goto fail;
        }
        /*first we need to unlink the vspace node*/
        nexus_rb_tree_vspace_remove(vspace_node,
                                    &(nexus_root->_vspace_rb_root));
        unlock_cas(&nexus_root->nexus_lock);

        lock_cas(&vspace_node->vs->nexus_vspace_lock);
        /*
                for release the vspace
                there's no need to use a tree to record one nexus node for
           release, which might lead to the stack overflow for a recursive
           algorithm so we must use a list entry to maintain the relationship,
                and the release the vspace is always using the
        */
        struct list_entry* curr = vspace_node->_vspace_list.next;
        struct list_entry* next;
        while (curr != &(vspace_node->_vspace_list)) {
                next = curr->next;
                struct nexus_node* node =
                        container_of(curr, struct nexus_node, _vspace_list);

                i64 ppn = unmap(vs,
                                VPN(node->addr),
                                0,
                                vspace_node->handler,
                                &(vs->vspace_lock));
                if (ppn < 0) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        goto fail;
                }

                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->handler->cpu_id));
                vspace_node->handler->pmm->pmm_free(ppn,
                                                    nexus_node_get_pages(node));
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock,
                                    vspace_node->handler->cpu_id));

                list_del_init(curr);
                /*no need to maintain the rb tree*/
                nexus_free_entry(node, vspace_node);
                curr = next;
        }
        unlock_cas(&vspace_node->vs->nexus_vspace_lock);

        lock_cas(&nexus_root->nexus_lock);
        /*free the manage page*/
        struct nexus_node* page_manage_node =
                (struct nexus_node*)ROUND_DOWN((vaddr)vspace_node, PAGE_SIZE);
        free_manage_node_with_page(page_manage_node, nexus_root);
        unlock_cas(&nexus_root->nexus_lock);
        return;
fail:
        return;
}
static inline void insert_nexus_entry(struct nexus_node* free_nexus_entry,
                                      vaddr addr, bool is_2M,
                                      ENTRY_FLAGS_t flags, VSpace* vs,
                                      struct nexus_node* vspace_root)
{
        free_nexus_entry->addr = addr;
        free_nexus_entry->region_flags = flags;
        nexus_node_set_len(free_nexus_entry, is_2M);
        free_nexus_entry->vs = vs;
        list_add_head(&(free_nexus_entry->_vspace_list),
                      &(vspace_root->_vspace_list));
        nexus_rb_tree_insert(free_nexus_entry, &(vspace_root->_rb_root));
}
static inline void delete_nexus_entry(struct nexus_node* nexus_entry,
                                      struct nexus_node* vspace_root)
{
        /*del from the vspace*/
        list_del_init(&(nexus_entry->_vspace_list));
        /*del from rb tree*/
        nexus_rb_tree_remove(nexus_entry, &vspace_root->_rb_root);
        nexus_free_entry(nexus_entry, vspace_root);
}
static struct nexus_node* _take_range(bool allow_2M, bool direct_map_ppn,
                                      ENTRY_FLAGS_t eflags,
                                      struct nexus_node* vspace_node,
                                      vaddr free_page_addr, vaddr page_addr_end)
{
        struct nexus_node* first_entry = NULL;
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
                                           vspace_node->vs,
                                           vspace_node);
                        if (!first_entry) {
                                first_entry = free_nexus_entry;
                        }
                        free_page_addr += MIDDLE_PAGE_SIZE;
                }

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
                                   vspace_node->vs,
                                   vspace_node);
                if (!first_entry) {
                        first_entry = free_nexus_entry;
                }
        }
        return first_entry;
fail:
        return NULL;
}
static void* _kernel_get_free_page(int page_num, enum zone_type memory_zone,
                                   ENTRY_FLAGS_t flags,
                                   struct nexus_node* nexus_root)
{
        vaddr free_page_addr, page_addr_end;
        struct nexus_node* first_entry = NULL;
        size_t alloced_page_number;
        lock_cas(&nexus_root->vs->nexus_vspace_lock);
        VSpace* vs = nexus_root->vs;
        ENTRY_FLAGS_t kernel_eflags = PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                      | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE;

        /*get phy pages from pmm*/
        lock_mcs(&nexus_root->handler->pmm->spin_ptr,
                 &per_cpu(pmm_spin_lock, nexus_root->handler->cpu_id));
        i64 ppn = nexus_root->handler->pmm->pmm_alloc(
                page_num, memory_zone, &alloced_page_number);
        unlock_mcs(&nexus_root->handler->pmm->spin_ptr,
                   &per_cpu(pmm_spin_lock, nexus_root->handler->cpu_id));
        if (ppn <= 0 || alloced_page_number < page_num) {
                pr_error("[ NEXUS ] ERROR: init error allocated %x\n",
                         alloced_page_number);
                goto fail;
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
                goto fail;
        }
        free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        page_addr_end = free_page_addr + page_num * PAGE_SIZE;

        first_entry = _take_range(true,
                                  true,
                                  kernel_eflags,
                                  nexus_root,
                                  free_page_addr,
                                  page_addr_end);

        while (first_entry) {
                if (first_entry->region_flags & PAGE_ENTRY_HUGE) {
                        error_t map_res =
                                map(vs,
                                    PPN(KERNEL_VIRT_TO_PHY(first_entry->addr)),
                                    VPN(first_entry->addr),
                                    2,
                                    kernel_eflags,
                                    nexus_root->handler,
                                    &nexus_root->vs->vspace_lock);
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: kernel get free page map error 2M\n");
                                delete_nexus_entry(first_entry, nexus_root);
                                goto fail;
                        }
                        page_num -= MIDDLE_PAGES;
                } else {
                        error_t map_res =
                                map(vs,
                                    PPN(KERNEL_VIRT_TO_PHY(first_entry->addr)),
                                    VPN(first_entry->addr),
                                    3,
                                    kernel_eflags,
                                    nexus_root->handler,
                                    &nexus_root->vs->vspace_lock);
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: kernel get free page map error\n");
                                delete_nexus_entry(first_entry, nexus_root);
                                goto fail;
                        }
                        page_num--;
                }
                if (page_num <= 0) {
                        break;
                }
                first_entry = nexus_rb_tree_next(first_entry);
        }

        unlock_cas(&nexus_root->vs->nexus_vspace_lock);
        return (void*)free_page_addr;
fail:
        unlock_cas(&nexus_root->vs->nexus_vspace_lock);
        return NULL;
}
/*
        we try to get the user physical pages and map the physical pages to the
   virtual pages which only based on the 4K pages in user space
*/
error_t user_fill_range(struct nexus_node* first_entry, int page_num,
                        enum zone_type memory_zone,
                        struct nexus_node* vspace_node, VSpace* vs)
{
        size_t alloced_page_number;
        if (!vspace_node)
                goto fail;

        lock_cas(&vspace_node->vs->nexus_vspace_lock);
        while (first_entry) {
                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->handler->cpu_id));
                i64 ppn = vspace_node->handler->pmm->pmm_alloc(
                        1, memory_zone, &alloced_page_number);
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock,
                                    vspace_node->handler->cpu_id));
                if (ppn <= 0 || alloced_page_number != 1) {
                        pr_error("[ NEXUS ] ERROR: init error allocated %x\n",
                                 alloced_page_number);
                        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
                        goto fail;
                } else if (alloced_page_number > 1) {
                        /*
                                if allocated page number is unequal to
                           the page number then the upper level cannot
                           get the allocated page number info and it
                           will not try to free the last pages then
                           those pages will not usable forever
                        */
                        pr_error(
                                "[ NEXUS ] ERROR: allocated pages is larger then needed pages\n");
                        pr_error(
                                "[ NEXUS ] HINT: try to alloc %d pages, allocated %d pages\n",
                                1,
                                alloced_page_number);
                        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
                        goto fail;
                }
                error_t map_res = map(vs,
                                      ppn,
                                      VPN(first_entry->addr),
                                      3,
                                      first_entry->region_flags,
                                      vspace_node->handler,
                                      &vspace_node->vs->vspace_lock);
                if (map_res) {
                        pr_error(
                                "[ NEXUS ] ERROR: kernel get free page map error\n");
                        delete_nexus_entry(first_entry, vspace_node);
                        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
                        goto fail;
                }
                page_num--;
                if (page_num <= 0) {
                        break;
                }
                first_entry = nexus_rb_tree_next(first_entry);
        }

        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
        return 0;
fail:
        return -E_RENDEZVOS;
}
static struct nexus_node* _user_take_range(int page_num, vaddr target_vaddr,
                                           struct nexus_node* vspace_node,
                                           VSpace* vs,
                                           ENTRY_FLAGS_t user_eflags)
{
        vaddr free_page_addr, page_addr_end;
        struct nexus_node* first_entry = NULL;
        /*and obviously, the address 0 should not accessed by any of the
         * user*/
        free_page_addr = (((u64)target_vaddr) >> 12) << 12;
        if (free_page_addr != target_vaddr || !vs || !vspace_node) {
                return NULL;
        }

        lock_cas(&vspace_node->vs->nexus_vspace_lock);
        /*adjust the flags, the user flags must not include PAGE_ENTRY_GLOBAL*/
        user_eflags = clear_mask(user_eflags, PAGE_ENTRY_GLOBAL);

        page_addr_end = free_page_addr + page_num * PAGE_SIZE;

        first_entry = _take_range(false,
                                  false,
                                  user_eflags,
                                  vspace_node,
                                  free_page_addr,
                                  page_addr_end);

        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
        return first_entry;
}
static error_t _release_range(void* p, int page_num, VSpace* vs,
                              struct nexus_node* vspace_node,
                              struct nexus_node* node)
{
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
                i64 ppn = unmap(vs,
                                VPN(node->addr),
                                0,
                                vspace_node->handler,
                                &vspace_node->vs->vspace_lock);
                if (ppn < 0) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
                        return -E_RENDEZVOS;
                }
                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->handler->cpu_id));
                vspace_node->handler->pmm->pmm_free(ppn,
                                                    nexus_node_get_pages(node));
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock,
                                    vspace_node->handler->cpu_id));

                node = nexus_rb_tree_next(node);
                if (node->manage_free_list.next
                    && node->manage_free_list.prev) {
                        need_break = true;
                }
                if (need_break)
                        break;
        }
        return 0;
}
static error_t _kernel_free_pages(void* p, int page_num,
                                  struct nexus_node* nexus_root)
{
        VSpace* vs = nexus_root->vs;
        vaddr free_end = (vaddr)p + page_num * PAGE_SIZE;
        /*in kernel alloc, only alloced one time but might mapped
         * several times*/
        lock_cas(&nexus_root->vs->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&nexus_root->_rb_root, (vaddr)p);
        if (!node) {
                /*
                        TODO: if cannot find the nexus node
                        which might caused by this page is under another core's
                   nexus
                */
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)nexus_root);
                unlock_cas(&nexus_root->vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }

        error_t res = _release_range(p, page_num, vs, nexus_root, node);
        if (res) {
                unlock_cas(&nexus_root->vs->nexus_vspace_lock);
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
                if (is_page_manage_node(node)) {
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

        unlock_cas(&nexus_root->vs->nexus_vspace_lock);
        return 0;
}
/*
        we try to unmap and free the physical pages but do not free the virtual
   ranges which only based on the 4K pages in user space remember that it must
   unmap first and then free it
*/
error_t user_unfill_range(void* p, int page_num, VSpace* vs,
                          struct nexus_node* vspace_node)
{
        lock_cas(&vspace_node->vs->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, (vaddr)p);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)vspace_node);
                unlock_cas(&vspace_node->vs->nexus_vspace_lock);
                return -E_IN_PARAM;
        }
        error_t res = _release_range(p, page_num, vs, vspace_node, node);
        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
        return res;
}

static error_t _user_release_range(void* p, int page_num, VSpace* vs,
                                   struct nexus_node* vspace_node)
{
        vaddr free_end = (vaddr)p + page_num * PAGE_SIZE;

        lock_cas(&vspace_node->vs->nexus_vspace_lock);
        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, (vaddr)p);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)vspace_node);
                unlock_cas(&vspace_node->vs->nexus_vspace_lock);
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
                if (is_page_manage_node(node)) {
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
        unlock_cas(&vspace_node->vs->nexus_vspace_lock);
        return 0;
}
void* get_free_page(int page_num, enum zone_type memory_zone,
                    vaddr target_vaddr, struct nexus_node* nexus_root,
                    VSpace* vs, ENTRY_FLAGS_t flags)
{
        /*first check the input parameter*/
        if (page_num < 0 || memory_zone < 0 || memory_zone > ZONE_NR_MAX
            || !nexus_root) {
                pr_error("[ NEXUS ] error input parameter\n");
                return NULL;
        }
        void* res = NULL;
        if (target_vaddr >= KERNEL_VIRT_OFFSET) {
                res = _kernel_get_free_page(
                        page_num, memory_zone, flags, nexus_root);
        } else {
                /*find the vspace root nexus node*/
                lock_cas(&nexus_root->nexus_lock);
                struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                        &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
                if (!vspace_node) {
                        pr_error("[Error] no such a vspace in nexus\n");
                        unlock_cas(&nexus_root->nexus_lock);
                        return NULL;
                }
                unlock_cas(&nexus_root->nexus_lock);
                struct nexus_node* first_entry = _user_take_range(
                        page_num, target_vaddr, vspace_node, vs, flags);
                if (first_entry
                    && !user_fill_range(first_entry,
                                        page_num,
                                        memory_zone,
                                        vspace_node,
                                        vs)) {
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
error_t free_pages(void* p, int page_num, VSpace* vs,
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
                lock_cas(&nexus_root->nexus_lock);
                struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                        &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
                unlock_cas(&nexus_root->nexus_lock);
                res = user_unfill_range(p, page_num, vs, vspace_node);
                if (!res)
                        res = _user_release_range(p, page_num, vs, vspace_node);
        }
        return res;
}