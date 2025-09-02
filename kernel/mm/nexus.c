#include <common/string.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/vmm.h>

spin_lock nexus_spin_lock_ptr;
DEFINE_PER_CPU(struct spin_lock_t, nexus_spin_lock);
static void nexus_rb_tree_insert(struct nexus_node* node,
                                 struct rb_root* vspace_root)
{
        struct rb_node** new = &vspace_root->rb_root, *parent = NULL;
        u64 key = node->start_addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node =
                        container_of(parent, struct nexus_node, _rb_node);
                if (key < (u64)tmp_node->start_addr)
                        new = &parent->left_child;
                else if (key > (u64)tmp_node->start_addr)
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
        while (node) {
                struct nexus_node* tmp_node =
                        container_of(node, struct nexus_node, _rb_node);
                if (start_addr < tmp_node->start_addr)
                        node = node->left_child;
                else if (start_addr > tmp_node->start_addr)
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
static void nexus_init_manage_page(vaddr vpage_addr,
                                   struct nexus_node* nexus_root)
{
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;
        /*init the node 0 point to this page*/
        n_node->start_addr = vpage_addr;
        /*the manage page is promised in kernel space, and use a identity
         * mapping*/
        n_node->ppn = KERNEL_VIRT_TO_PHY(vpage_addr);
        n_node->size = 1;
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
        n_node[1].backup_manage_page = NULL;
        n_node[1].handler = handler;
        n_node[1].nexus_id = handler->cpu_id;
        n_node[1].vs = vs;
        INIT_LIST_HEAD(&n_node[1].manage_free_list);
        INIT_LIST_HEAD(&n_node[1]._vspace_list);
        lock_init_cas(&n_node[1].lock);
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
        i64 nexus_init_page = handler->pmm->pmm_alloc(1, ZONE_NORMAL);
        if (nexus_init_page <= 0) {
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
        /*from manage_free_list find one manage page that have free node*/
        struct list_entry* manage_free_list_node = &root_node->manage_free_list;
        struct list_entry* lp = manage_free_list_node->next;
        if (lp == manage_free_list_node) {
                /*if we have backend page,just use it*/
                void* backup_page = root_node->backup_manage_page;
                if (backup_page) {
                        lp = &((struct nexus_node*)backup_page)
                                      ->manage_free_list;
                        root_node->backup_manage_page = NULL;
                        list_add_head(&(((struct nexus_node*)backup_page)
                                                ->_vspace_list),
                                      &(root_node->_vspace_list));
                        nexus_rb_tree_insert((struct nexus_node*)backup_page,
                                             &root_node->_rb_root);
                } else {
                        /*means no free manage can use, try alloc a new one*/
                        VSpace* vs = root_node->vs;

                        lock_mcs(&root_node->handler->pmm->spin_ptr,
                                 &per_cpu(pmm_spin_lock, root_node->nexus_id));
                        i64 nexus_new_page = root_node->handler->pmm->pmm_alloc(
                                1, ZONE_NORMAL);
                        unlock_mcs(&root_node->handler->pmm->spin_ptr,
                                   &per_cpu(pmm_spin_lock,
                                            root_node->nexus_id));
                        if (nexus_new_page <= 0) {
                                pr_error("[ NEXUS ] ERROR: init error\n");
                                return NULL;
                        }
                        vaddr vpage_addr =
                                KERNEL_PHY_TO_VIRT(PADDR(nexus_new_page));
                        error_t map_res = map(
                                vs,
                                nexus_new_page,
                                VPN(vpage_addr),
                                3,
                                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                                root_node->handler,
                                &kspace_spin_lock_ptr);
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: get free entry map error\n");
                                return NULL;
                        }
                        memset((void*)vpage_addr, '\0', PAGE_SIZE);
                        nexus_init_manage_page(vpage_addr, root_node);
                        lp = &((struct nexus_node*)vpage_addr)->manage_free_list;
                }
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
        i64 ppn = (i64)PPN(KERNEL_VIRT_TO_PHY((vaddr)page_manage_node));
        error_t unmap_res = unmap(vspace_root->vs,
                                  VPN((vaddr)page_manage_node),
                                  vspace_root->handler,
                                  &kspace_spin_lock_ptr);
        if (unmap_res) {
                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                return;
        }
        lock_mcs(&vspace_root->handler->pmm->spin_ptr,
                 &per_cpu(pmm_spin_lock, vspace_root->nexus_id));
        vspace_root->handler->pmm->pmm_free(ppn, 1);
        unlock_mcs(&vspace_root->handler->pmm->spin_ptr,
                   &per_cpu(pmm_spin_lock, vspace_root->nexus_id));
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
                if (!(nexus_root->backup_manage_page)) {
                        /*if no backup page, just make it as the backup page*/
                        nexus_root->backup_manage_page =
                                (void*)page_manage_node;
                } else if ((vaddr)(nexus_root->backup_manage_page)
                           != (vaddr)page_manage_node) {
                        /*if it's the backup page, no need to del it*/
                        free_manage_node_with_page(page_manage_node,
                                                   nexus_root);
                }
        }
}
struct nexus_node* nexus_create_vspace_root_node(struct nexus_node* nexus_root,
                                                 VSpace* vs)
{
        /*try to find the vs paddr root ,if exist, error*/
        if (!nexus_root || !vs) {
                pr_error("[Error] input parameter error\n");
                goto fail;
        }
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (vspace_node) {
                pr_error("[Error] have has such a vspace in nexus\n");
                goto fail;
        }

        /*get a phy page*/
        i64 nexus_init_page =
                nexus_root->handler->pmm->pmm_alloc(1, ZONE_NORMAL);
        if (nexus_init_page <= 0) {
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
                nexus_root->handler,
                NULL)) {
                pr_error("[ NEXUS ] ERROR: init nexus map error\n");
                return NULL;
        }
        return init_vspace_nexus(vpage_addr,
                                 vs,
                                 nexus_root->handler,
                                 &nexus_root->_vspace_rb_root);
fail:
        return NULL;
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
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &src_nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                goto fail;
        }
        vspace_node->nexus_id = dst_nexus_root->nexus_id;
        nexus_rb_tree_vspace_remove(vspace_node,
                                    &(src_nexus_root->_vspace_rb_root));
        nexus_rb_tree_vspace_insert(vspace_node,
                                    &dst_nexus_root->_vspace_rb_root);
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
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                goto fail;
        }
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

                error_t unmap_res = unmap(vs,
                                          VPN(node->start_addr),
                                          vspace_node->handler,
                                          &(vs->vspace_lock));
                if (unmap_res) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        goto fail;
                }

                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                vspace_node->handler->pmm->pmm_free((i64)(node->ppn),
                                                    node->size);
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, vspace_node->nexus_id));

                list_del_init(curr);
                /*no need to maintain the rb tree*/
                nexus_free_entry(node, vspace_node);
                curr = next;
        }
        /*after we delete the map nodes,we need to delete the vspace node*/
        nexus_rb_tree_vspace_remove(vspace_node,
                                    &(nexus_root->_vspace_rb_root));

        /*free the manage page*/
        struct nexus_node* page_manage_node =
                (struct nexus_node*)ROUND_DOWN((vaddr)vspace_node, PAGE_SIZE);
        free_manage_node_with_page(page_manage_node, nexus_root);
        return;
fail:
        return;
}
static void* _kernel_get_free_page(int page_num, enum zone_type memory_zone,
                                   vaddr target_vaddr,
                                   struct nexus_node* nexus_root)
{
        vaddr free_page_addr;
        /*try get a free entry*/
        struct nexus_node* free_nexus_entry = nexus_get_free_entry(nexus_root);
        if (!free_nexus_entry) {
                pr_error("[ NEXUS ] cannot find a new free nexus entry\n");
                goto fail;
        }
        VSpace* vs = nexus_root->vs;
        /*get phy pages from pmm*/
        /*in kernel, we promise that we should not try to alloc a space
         * more then 2M*/
        if (page_num > MIDDLE_PAGES) {
                pr_error(
                        "[ NEXUS ] ERROR: try to get tooo much pages in kernel\n");
                /*we have alloc a new usable entry ,we need to return
                 * back*/
                goto fail_free_nexus_entry;
        }
        lock_mcs(&nexus_root->handler->pmm->spin_ptr,
                 &per_cpu(pmm_spin_lock, nexus_root->nexus_id));
        i64 ppn = nexus_root->handler->pmm->pmm_alloc(page_num, memory_zone);
        unlock_mcs(&nexus_root->handler->pmm->spin_ptr,
                   &per_cpu(pmm_spin_lock, nexus_root->nexus_id));
        if (ppn <= 0) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                /*we have alloc a new usable entry ,we need to return
                 * back*/
                goto fail_free_nexus_entry;
        }
        free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        /*map, here remember, if alloc a 2M huge page, just map a level
         * 2 page*/
        if (page_num > MIDDLE_PAGES / 2) /*buddy pmm must alloc a 2M
                                            page*/
        {
                error_t map_res =
                        map(vs,
                            ppn,
                            VPN(free_page_addr),
                            2,
                            PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                    | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                            nexus_root->handler,
                            &kspace_spin_lock_ptr);
                if (map_res) {
                        pr_error(
                                "[ NEXUS ] ERROR: kernel get free page map error 2M\n");
                        goto fail_free_nexus_entry;
                }
        } else {
                int error_num = -E_RENDEZVOS;
                for (int i = 0, tmp_ppn = ppn; i < page_num;
                     i++, tmp_ppn += 1) {
                        vaddr tmp_free_page_addr =
                                KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                        error_t map_res = map(
                                vs,
                                tmp_ppn,
                                VPN(tmp_free_page_addr),
                                3,
                                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                                nexus_root->handler,
                                &kspace_spin_lock_ptr);
                        if (map_res) {
                                pr_error(
                                        "[ NEXUS ] ERROR: kernel get free page map error\n");
                                error_num = i;
                        }
                }
                if (error_num != -E_RENDEZVOS) {
                        /*unmap them all*/
                        for (int i = 0, tmp_ppn = ppn; i <= error_num;
                             i++, tmp_ppn += 1) {
                                vaddr tmp_free_page_addr =
                                        KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                                error_t unmap_res =
                                        unmap(vs,
                                              VPN(tmp_free_page_addr),
                                              nexus_root->handler,
                                              &kspace_spin_lock_ptr);
                                if (unmap_res) {
                                        pr_error(
                                                "[ NEXUS ] ERROR: map success but unmap have error\n");
                                        goto fail_free_nexus_entry;
                                }
                        }
                        goto fail_free_nexus_entry;
                }
        }
        /*fill in the entry and link it*/
        free_nexus_entry->start_addr = free_page_addr;
        free_nexus_entry->size = page_num;
        free_nexus_entry->ppn = ppn;
        free_nexus_entry->vs = nexus_root->vs;
        /*we directly insert this node to the nexus_root as kernel page tree*/
        list_add_head(&(free_nexus_entry->_vspace_list),
                      &(nexus_root->_vspace_list));
        nexus_rb_tree_insert(free_nexus_entry, &nexus_root->_rb_root);
        return (void*)free_page_addr;
fail_free_nexus_entry:
        nexus_free_entry(free_nexus_entry, nexus_root);
fail:
        return NULL;
}
static void* _user_get_free_page(int page_num, enum zone_type memory_zone,
                                 vaddr target_vaddr,
                                 struct nexus_node* nexus_root, VSpace* vs,
                                 ENTRY_FLAGS_t flags)
{
        vaddr free_page_addr;
        /*and obviously, the address 0 should not accessed by any of the
         * user*/
        free_page_addr = (((u64)target_vaddr) >> 12) << 12;
        if (free_page_addr != target_vaddr || !vs) {
                return NULL;
        }
        /*find the vspace root nexus node*/
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        if (!vspace_node) {
                pr_error("[Error] no such a vspace in nexus\n");
                return NULL;
        }

        /*adjust the flags, the user flags must not include PAGE_ENTRY_GLOBAL*/
        flags = clear_mask(flags, PAGE_ENTRY_GLOBAL);

        /*first try to map 2M pages*/
        int alloced_pages;
        for (alloced_pages = 0; alloced_pages + MIDDLE_PAGES <= page_num;
             alloced_pages += MIDDLE_PAGES) {
                if (ROUND_DOWN(target_vaddr, MIDDLE_PAGE_SIZE)
                    != target_vaddr) {
                        return NULL;
                }
                struct nexus_node* free_nexus_entry =
                        nexus_get_free_entry(vspace_node);
                if (!free_nexus_entry) {
                        pr_error(
                                "[ NEXUS ] cannot find a new free nexus entry\n");
                        return NULL;
                }
                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                i64 ppn = vspace_node->handler->pmm->pmm_alloc(MIDDLE_PAGES,
                                                               memory_zone);
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                if (ppn <= 0) {
                        pr_error("[ NEXUS ] ERROR: init error\n");
                        /*we have alloc a new usable entry ,we need to
                         * return back*/
                        nexus_free_entry(free_nexus_entry, vspace_node);
                        return NULL;
                }
                error_t map_res = map(vs,
                                      ppn,
                                      VPN(target_vaddr),
                                      2,
                                      PAGE_ENTRY_USER | flags,
                                      vspace_node->handler,
                                      &(vspace_node->vs->vspace_lock));
                if (map_res) {
                        pr_error(
                                "[ NEXUS ] ERROR: user get free page map error 2M\n");
                        nexus_free_entry(free_nexus_entry, vspace_node);
                        return NULL;
                }
                free_nexus_entry->start_addr = target_vaddr;
                free_nexus_entry->size = MIDDLE_PAGES;
                free_nexus_entry->ppn = ppn;
                free_nexus_entry->vs = vs;
                list_add_head(&(free_nexus_entry->_vspace_list),
                              &(vspace_node->_vspace_list));
                nexus_rb_tree_insert(free_nexus_entry, &vspace_node->_rb_root);
                target_vaddr += MIDDLE_PAGE_SIZE;
        }
        for (; alloced_pages < page_num; alloced_pages++) {
                struct nexus_node* free_nexus_entry =
                        nexus_get_free_entry(vspace_node);
                if (!free_nexus_entry) {
                        pr_error(
                                "[ NEXUS ] cannot find a new free nexus entry\n");
                        return NULL;
                }
                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                i64 ppn = vspace_node->handler->pmm->pmm_alloc(1, memory_zone);
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                if (ppn <= 0) {
                        pr_error("[ NEXUS ] ERROR: init error\n");
                        /*we have alloc a new usable entry ,we need to
                         * return back*/
                        nexus_free_entry(free_nexus_entry, vspace_node);
                        return NULL;
                }
                error_t map_res = map(vs,
                                      ppn,
                                      VPN(target_vaddr),
                                      3,
                                      PAGE_ENTRY_USER | flags,
                                      vspace_node->handler,
                                      &(vspace_node->vs->vspace_lock));
                if (map_res) {
                        pr_error(
                                "[ NEXUS ] ERROR: user get free page map error\n");
                        nexus_free_entry(free_nexus_entry, vspace_node);
                        return NULL;
                }
                free_nexus_entry->start_addr = target_vaddr;
                free_nexus_entry->size = 1;
                free_nexus_entry->ppn = ppn;
                free_nexus_entry->vs = vs;
                list_add_head(&(free_nexus_entry->_vspace_list),
                              &(vspace_node->_vspace_list));
                nexus_rb_tree_insert(free_nexus_entry, &vspace_node->_rb_root);
                target_vaddr += PAGE_SIZE;
        }
        return (void*)free_page_addr;
}
static error_t _kernel_free_pages(void* p, int page_num,
                                  struct nexus_node* nexus_root)
{
        VSpace* vs = nexus_root->vs;
        /*in kernel alloc, only alloced one time but might mapped
         * several times*/
        /* for kernel ,we directly */
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
                return -E_IN_PARAM;
        }
        if (page_num != node->size && page_num != 0) {
                /*
                for kernel space, nexus entry have record the page size
                and it must be free pages with num equal to that size
                so we let 0 also be legal
                */
                pr_error(
                        "[ NEXUS ] ERROR: we cannot free pages has different number 0x%x when you alloc in kernel 0x%x\n",
                        page_num,
                        node->size);
                return -E_IN_PARAM;
        }
        i64 ppn = (i64)(node->ppn);
        vaddr map_addr = node->start_addr;
        if (node->size > MIDDLE_PAGES / 2) {
                error_t unmap_res = unmap(vs,
                                          VPN(map_addr),
                                          nexus_root->handler,
                                          &kspace_spin_lock_ptr);
                if (unmap_res) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        return -E_RENDEZVOS;
                }
        } else {
                for (int i = 0; i < node->size; i++) {
                        error_t unmap_res = unmap(vs,
                                                  VPN(map_addr),
                                                  nexus_root->handler,
                                                  &kspace_spin_lock_ptr);
                        if (unmap_res) {
                                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                                return -E_RENDEZVOS;
                        }
                        map_addr += PAGE_SIZE;
                }
        }
        lock_mcs(&nexus_root->handler->pmm->spin_ptr,
                 &per_cpu(pmm_spin_lock, nexus_root->nexus_id));
        nexus_root->handler->pmm->pmm_free(ppn, node->size);
        unlock_mcs(&nexus_root->handler->pmm->spin_ptr,
                   &per_cpu(pmm_spin_lock, nexus_root->nexus_id));
        /*del from the vspace*/
        list_del_init(&(node->_vspace_list));
        /*del from rb tree*/
        nexus_rb_tree_remove(node, &nexus_root->_rb_root);
        nexus_free_entry(node, nexus_root);
        return 0;
}
static error_t _user_free_pages(void* p, int page_num, VSpace* vs,
                                struct nexus_node* nexus_root)
{
        struct nexus_node* vspace_node = nexus_rb_tree_vspace_search(
                &nexus_root->_vspace_rb_root, vs->vspace_root_addr);
        struct nexus_node* node =
                nexus_rb_tree_search(&vspace_node->_rb_root, (vaddr)p);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)nexus_root);
                return -E_IN_PARAM;
        }
        while (1) {
                i64 ppn = (i64)(node->ppn);
                vaddr map_addr = node->start_addr;
                u64 size = node->size;
                page_num -= size;
                if (page_num < 0) {
                        pr_error(
                                "[ NEXUS ] ERROR: the size is unequal with the alloc time, this vspace might be wrong\n");
                        return -E_RENDEZVOS;
                } else if (page_num == 0) {
                        break;
                }
                vaddr expect_next_addr = map_addr + size * PAGE_SIZE;
                error_t unmap_res = unmap(vs,
                                          VPN(map_addr),
                                          vspace_node->handler,
                                          &(vs->vspace_lock));
                if (unmap_res) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        return -E_RENDEZVOS;
                }
                lock_mcs(&vspace_node->handler->pmm->spin_ptr,
                         &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                vspace_node->handler->pmm->pmm_free(ppn, node->size);
                unlock_mcs(&vspace_node->handler->pmm->spin_ptr,
                           &per_cpu(pmm_spin_lock, vspace_node->nexus_id));
                list_del_init(&(node->_vspace_list));
                nexus_rb_tree_remove(node, &vspace_node->_rb_root);
                nexus_free_entry(node, vspace_node);
                struct rb_node* next_rb = RB_Next(&node->_rb_node);
                if (!next_rb)
                        break;
                node = container_of(next_rb, struct nexus_node, _rb_node);
                if (node->start_addr != expect_next_addr) {
                        pr_error(
                                "[ NEXUS ] ERROR: the range is not continuous\n");
                        return -E_RENDEZVOS;
                }
        }
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
        lock_cas(&nexus_root->lock);
        if (target_vaddr >= KERNEL_VIRT_OFFSET) {
                res = _kernel_get_free_page(
                        page_num, memory_zone, target_vaddr, nexus_root);
        } else {
                res = _user_get_free_page(page_num,
                                          memory_zone,
                                          target_vaddr,
                                          nexus_root,
                                          vs,
                                          flags);
        }
        unlock_cas(&nexus_root->lock);
        return res;
}
error_t free_pages(void* p, int page_num, VSpace* vs,
                   struct nexus_node* nexus_root)
{
        if (!p || !nexus_root || (((vaddr)p) & 0xfff)) {
                pr_error("[ ERROR ] ERROR: error input arg\n");
                return -E_IN_PARAM;
        }
        error_t res = 0;
        lock_cas(&nexus_root->lock);
        if ((vaddr)p >= KERNEL_VIRT_OFFSET) {
                res = _kernel_free_pages(p, page_num, nexus_root);
        } else {
                res = _user_free_pages(p, page_num, vs, nexus_root);
        }
        unlock_cas(&nexus_root->lock);
        return res;
}