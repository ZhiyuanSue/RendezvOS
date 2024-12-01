#include <shampoos/mm/nexus.h>
#include <shampoos/mm/vmm.h>
#include <common/string.h>
#include <modules/log/log.h>
#include <shampoos/error.h>

static void nexus_rb_tree_insert(struct nexus_node* node, struct rb_root* root)
{
        struct rb_node** new = &root->rb_root, *parent = NULL;
        u64 key1 = node->vspace_root;
        u64 key2 = node->start_addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node =
                        container_of(parent, struct nexus_node, _rb_node);
                if (key1 < (u64)tmp_node->vspace_root)
                        new = &parent->left_child;
                else if (key1 > (u64)tmp_node->vspace_root)
                        new = &parent->right_child;
                else {
                        if (key2 < (u64)tmp_node->start_addr)
                                new = &parent->left_child;
                        else if (key2 > (u64)tmp_node->start_addr)
                                new = &parent->right_child;
                        else
                                return;
                }
        }
        RB_Link_Node(&node->_rb_node, parent, new);
        RB_SolveDoubleRed(&node->_rb_node, root);
}
static void nexus_rb_tree_remove(struct nexus_node* node, struct rb_root* root)
{
        RB_Remove(&node->_rb_node, root);
        node->_rb_node.black_height = 0;
        node->_rb_node.left_child = node->_rb_node.right_child = NULL;
        node->_rb_node.rb_parent_color = 0;
}
struct nexus_node* nexus_rb_tree_search(struct rb_root* root, vaddr start_addr,
                                        paddr vspace_root)
{
        struct rb_node* node = root->rb_root;
        while (node) {
                struct nexus_node* tmp_node =
                        container_of(node, struct nexus_node, _rb_node);
                if (vspace_root < tmp_node->vspace_root) {
                        node = node->left_child;
                } else if (vspace_root > tmp_node->vspace_root) {
                        node = node->right_child;
                } else {
                        if (start_addr < tmp_node->start_addr)
                                node = node->left_child;
                        else if (start_addr > tmp_node->start_addr)
                                node = node->right_child;
                        else
                                return tmp_node;
                }
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
struct nexus_node* nexus_rb_tree_search_vspace(struct rb_root* root,
                                               paddr vspace_root)
{
        struct rb_node* node = root->rb_root;
        struct nexus_node* curr_node = NULL;
        while (node) {
                struct nexus_node* tmp_node =
                        container_of(node, struct nexus_node, _rb_node);
                if (vspace_root < tmp_node->vspace_root) {
                        node = node->left_child;
                } else if (vspace_root > tmp_node->vspace_root) {
                        node = node->right_child;
                } else {
                        curr_node = tmp_node;
                }
        }
        struct nexus_node* prev_node = nexus_rb_tree_prev(curr_node);
        while (curr_node && prev_node
               && prev_node->vspace_root == vspace_root) {
                curr_node = prev_node;
                prev_node = nexus_rb_tree_prev(curr_node);
        }
        return curr_node;
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
        nexus_rb_tree_insert(n_node, &nexus_root->_rb_root);
}
/*return a nexus root node*/
struct nexus_node* init_nexus(struct map_handler* handler)
{
        paddr vspace_root = get_current_kernel_vspace_root();
        /*get a phy page*/
        // TODO: add lock
        int nexus_init_page = handler->pmm->pmm_alloc(1, ZONE_NORMAL);
        // TODO: add unlock
        if (nexus_init_page <= 0) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                return NULL;
        }
        /*get a vir page with Identical mapping*/
        vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_init_page));
        if (map(&vspace_root,
                nexus_init_page,
                VPN(vpage_addr),
                3,
                PAGE_ENTRY_NONE,
                handler)) {
                pr_error("[ NEXUS ] ERROR: map error\n");
                return NULL;
        }
        /*remember clean this page*/
        memset((void*)vpage_addr, '\0', PAGE_SIZE);
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;

        /*init the node 1 as the root and let the nexus_root point to it*/
        struct nexus_node* root_node = &n_node[1];
        n_node[1].backup_manage_page = NULL;
        n_node[1].handler = handler;
        INIT_LIST_HEAD(&n_node[1].manage_free_list);

        nexus_init_manage_page(vpage_addr, &n_node[1]);
        /*you have to del the root node from the free list,for in init manage
         * page, it have been linked*/
        list_del_init(&n_node[1]._free_list);

        n_node->page_left_nexus -= 1;
        list_add_head(&n_node->manage_free_list, &root_node->manage_free_list);
        return &n_node[1];
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
                        nexus_rb_tree_insert((struct nexus_node*)backup_page,
                                             &root_node->_rb_root);
                } else {
                        /*means no free manage can use, try alloc a new one*/
                        paddr vspace_root = get_current_kernel_vspace_root();
                        // TODO:add lock
                        int nexus_new_page = root_node->handler->pmm->pmm_alloc(
                                1, ZONE_NORMAL);
                        // TODO:add unlock
                        if (nexus_new_page <= 0) {
                                pr_error("[ NEXUS ] ERROR: init error\n");
                                return NULL;
                        }
                        vaddr vpage_addr =
                                KERNEL_PHY_TO_VIRT(PADDR(nexus_new_page));
                        if (map(&vspace_root,
                                nexus_new_page,
                                VPN(vpage_addr),
                                3,
                                PAGE_ENTRY_NONE,
                                root_node->handler)) {
                                pr_error("[ NEXUS ] ERROR: map error\n");
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
                nexus_rb_tree_remove(page_manage_node, &nexus_root->_rb_root);
                if (!(nexus_root->backup_manage_page)) {
                        /*if no backup page, just make it as the backup page*/
                        nexus_root->backup_manage_page =
                                (void*)page_manage_node;
                } else if ((vaddr)(nexus_root->backup_manage_page)
                           != (vaddr)page_manage_node) {
                        /*if it's the backup page, no need to del it*/
                        /*free this manage page*/
                        paddr vspace_root = get_current_kernel_vspace_root();
                        int ppn = PPN(
                                KERNEL_VIRT_TO_PHY((vaddr)page_manage_node));
                        if (unmap(vspace_root,
                                  VPN((vaddr)page_manage_node),
                                  nexus_root->handler)) {
                                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                                return;
                        }
                        nexus_root->handler->pmm->pmm_free(ppn, 1);
                }
        }
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
                return NULL;
        }
        paddr vspace_root = get_current_kernel_vspace_root();
        /*get phy pages from pmm*/
        /*in kernel, we promise that we should not try to alloc a space
         * more then 2M*/
        if (page_num > MIDDLE_PAGES) {
                pr_error(
                        "[ NEXUS ] ERROR: try to get tooo much pages in kernel\n");
                /*we have alloc a new usable entry ,we need to return
                 * back*/
                nexus_free_entry(free_nexus_entry, nexus_root);
                return NULL;
        }
        // TODO: add lock
        int ppn = nexus_root->handler->pmm->pmm_alloc(page_num, ZONE_NORMAL);
        // TODO: add lock
        if (ppn <= 0) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                /*we have alloc a new usable entry ,we need to return
                 * back*/
                nexus_free_entry(free_nexus_entry, nexus_root);
                return NULL;
        }
        free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        /*map, here remember, if alloc a 2M huge page, just map a level
         * 2 page*/
        if (page_num > MIDDLE_PAGES / 2) /*buddy pmm must alloc a 2M
                                            page*/
        {
                if (map(&vspace_root,
                        ppn,
                        VPN(free_page_addr),
                        2,
                        PAGE_ENTRY_NONE,
                        nexus_root->handler)) {
                        pr_error("[ NEXUS ] ERROR: map error\n");
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
        } else {
                int error_num = -1;
                for (int i = 0, tmp_ppn = ppn; i < page_num;
                     i++, tmp_ppn += 1) {
                        vaddr tmp_free_page_addr =
                                KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                        if (map(&vspace_root,
                                tmp_ppn,
                                VPN(tmp_free_page_addr),
                                3,
                                PAGE_ENTRY_NONE,
                                nexus_root->handler)) {
                                pr_error("[ NEXUS ] ERROR: map error\n");
                                error_num = i;
                        }
                }
                if (error_num != -1) {
                        /*unmap them all*/
                        for (int i = 0, tmp_ppn = ppn; i <= error_num;
                             i++, tmp_ppn += 1) {
                                vaddr tmp_free_page_addr =
                                        KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                                if (unmap(vspace_root,
                                          VPN(tmp_free_page_addr),
                                          nexus_root->handler)) {
                                        pr_error(
                                                "[ NEXUS ] ERROR: map success but unmap have error\n");
                                        return NULL;
                                }
                        }
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
        }
        /*fill in the entry and link it*/
        free_nexus_entry->start_addr = free_page_addr;
        free_nexus_entry->size = page_num;
        free_nexus_entry->ppn = ppn;
        free_nexus_entry->vspace_root = 0;
        free_nexus_entry->nexus_id = nexus_root->nexus_id;
        nexus_rb_tree_insert(free_nexus_entry, &nexus_root->_rb_root);
        return (void*)free_page_addr;
}
static void* _user_get_free_page(int page_num, enum zone_type memory_zone,
                                 vaddr target_vaddr, paddr vspace_root,
                                 struct nexus_node* nexus_root)
{
        vaddr free_page_addr;
        /*and obviously, the address 0 should not accessed by any of the
         * user*/
        free_page_addr = (((u64)target_vaddr) >> 12) << 12;
        if (free_page_addr != target_vaddr) {
                return NULL;
        }
        /*first try to map 2M pages*/
        int alloced_pages;
        for (alloced_pages = 0; alloced_pages + MIDDLE_PAGES <= page_num;
             alloced_pages += MIDDLE_PAGES) {
                struct nexus_node* free_nexus_entry =
                        nexus_get_free_entry(nexus_root);
                if (!free_nexus_entry) {
                        pr_error(
                                "[ NEXUS ] cannot find a new free nexus entry\n");
                        return NULL;
                }
                // TODO: add lock
                int ppn = nexus_root->handler->pmm->pmm_alloc(MIDDLE_PAGES,
                                                              ZONE_NORMAL);
                // TODO: add lock
                if (ppn <= 0) {
                        pr_error("[ NEXUS ] ERROR: init error\n");
                        /*we have alloc a new usable entry ,we need to
                         * return back*/
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
                if (map(&vspace_root,
                        ppn,
                        VPN(target_vaddr),
                        2,
                        PAGE_ENTRY_USER,
                        nexus_root->handler)) {
                        pr_error("[ NEXUS ] ERROR: map error\n");
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
                free_nexus_entry->start_addr = target_vaddr;
                free_nexus_entry->size = MIDDLE_PAGES;
                free_nexus_entry->ppn = ppn;
                free_nexus_entry->vspace_root = vspace_root;
                free_nexus_entry->nexus_id = nexus_root->nexus_id;
                nexus_rb_tree_insert(free_nexus_entry, &nexus_root->_rb_root);
                target_vaddr += MIDDLE_PAGE_SIZE;
        }
        for (; alloced_pages < page_num; alloced_pages++) {
                struct nexus_node* free_nexus_entry =
                        nexus_get_free_entry(nexus_root);
                if (!free_nexus_entry) {
                        pr_error(
                                "[ NEXUS ] cannot find a new free nexus entry\n");
                        return NULL;
                }
                // TODO: add lock
                int ppn = nexus_root->handler->pmm->pmm_alloc(1, ZONE_NORMAL);
                // TODO: add lock
                if (ppn <= 0) {
                        pr_error("[ NEXUS ] ERROR: init error\n");
                        /*we have alloc a new usable entry ,we need to
                         * return back*/
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
                if (map(&vspace_root,
                        ppn,
                        VPN(target_vaddr),
                        3,
                        PAGE_ENTRY_USER,
                        nexus_root->handler)) {
                        pr_error("[ NEXUS ] ERROR: map error\n");
                        nexus_free_entry(free_nexus_entry, nexus_root);
                        return NULL;
                }
                free_nexus_entry->start_addr = target_vaddr;
                free_nexus_entry->size = 1;
                free_nexus_entry->ppn = ppn;
                free_nexus_entry->vspace_root = vspace_root;
                free_nexus_entry->nexus_id = nexus_root->nexus_id;
                nexus_rb_tree_insert(free_nexus_entry, &nexus_root->_rb_root);
                target_vaddr += PAGE_SIZE;
        }
        return (void*)free_page_addr;
}
void* get_free_page(int page_num, enum zone_type memory_zone,
                    vaddr target_vaddr, paddr vspace_root,
                    struct nexus_node* nexus_root)
{
        /*first check the input parameter*/
        if (page_num < 0 || memory_zone < 0 || memory_zone > ZONE_NR_MAX) {
                pr_error("[ NEXUS ] error input parameter\n");
                return NULL;
        }
        if (target_vaddr >= KERNEL_VIRT_OFFSET) {
                return _kernel_get_free_page(
                        page_num, memory_zone, target_vaddr, nexus_root);
        } else {
                return _user_get_free_page(page_num,
                                           memory_zone,
                                           target_vaddr,
                                           vspace_root,
                                           nexus_root);
        }
}
static error_t _kernel_free_pages(void* p, int page_num,
                                  struct nexus_node* nexus_root)
{
        paddr vspace_root = get_current_kernel_vspace_root();
        /*in kernel alloc, only alloced one time but might mapped
         * several times*/
        struct nexus_node* node =
                nexus_rb_tree_search(&nexus_root->_rb_root, (vaddr)p, 0);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)nexus_root);
                return -EINVAL;
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
                return -EINVAL;
        }
        u32 ppn = node->ppn;
        vaddr map_addr = node->start_addr;
        if (node->size > MIDDLE_PAGES / 2) {
                if (unmap(vspace_root, VPN(map_addr), nexus_root->handler)) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        return -ENOMEM;
                }
        } else {
                for (int i = 0; i < node->size; i++) {
                        if (unmap(vspace_root,
                                  VPN(map_addr),
                                  nexus_root->handler)) {
                                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                                return -ENOMEM;
                        }
                        map_addr += PAGE_SIZE;
                }
        }
        nexus_root->handler->pmm->pmm_free(ppn, node->size);
        /*del from rb tree*/
        nexus_rb_tree_remove(node, &nexus_root->_rb_root);
        nexus_free_entry(node, nexus_root);
        return 0;
}
static error_t _user_free_pages(void* p, int page_num, paddr vspace_root,
                                struct nexus_node* nexus_root)
{
        struct nexus_node* node = nexus_rb_tree_search(
                &nexus_root->_rb_root, (vaddr)p, vspace_root);
        if (!node) {
                pr_error(
                        "[ NEXUS ] ERROR: search the free page fail 0x%x 0x%x\n",
                        (vaddr)p,
                        (vaddr)nexus_root);
                return -EINVAL;
        }
        while (1) {
                u32 ppn = node->ppn;
                vaddr map_addr = node->start_addr;
                u64 size = node->size;
                vaddr expect_next_addr = map_addr + size * PAGE_SIZE;
                if (unmap(vspace_root, VPN(map_addr), nexus_root->handler)) {
                        pr_error("[ NEXUS ] ERROR: unmap error!\n");
                        return -ENOMEM;
                }
                nexus_root->handler->pmm->pmm_free(ppn, node->size);
                nexus_rb_tree_remove(node, &nexus_root->_rb_root);
                nexus_free_entry(node, nexus_root);
                page_num -= size;
                if (page_num < 0) {
                        pr_error(
                                "[ NEXUS ] ERROR: the size is unequal with the alloc time, this vspace might be wrong\n");
                        return -EINVAL;
                } else if (page_num == 0) {
                        break;
                }
                struct rb_node* next_rb = RB_Next(&node->_rb_node);
                if (!next_rb)
                        break;
                node = container_of(next_rb, struct nexus_node, _rb_node);
                if (node->start_addr != expect_next_addr) {
                        pr_error(
                                "[ NEXUS ] ERROR: the range is not continuous\n");
                        return -EINVAL;
                }
        }
        return 0;
}
error_t free_pages(void* p, int page_num, paddr vspace_root,
                   struct nexus_node* nexus_root)
{
        if (!p || !nexus_root || (((vaddr)p) & 0xfff)) {
                pr_error("[ ERROR ] ERROR: error input arg\n");
                return -EINVAL;
        }
        if ((vaddr)p >= KERNEL_VIRT_OFFSET) {
                return _kernel_free_pages(p, page_num, nexus_root);
        } else {
                return _user_free_pages(p, page_num, vspace_root, nexus_root);
        }
}