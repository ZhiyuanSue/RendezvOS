#include <shampoos/mm/nexus.h>
#include <shampoos/mm/vmm.h>
#include <common/string.h>
#include <modules/log/log.h>
#include <shampoos/error.h>

static void nexus_rb_tree_insert(struct nexus_node* node, struct rb_root* root)
{
        struct rb_node** new = &root->rb_root, *parent = NULL;
        u64 key = node->start_addr;
        while (*new) {
                parent = *new;
                struct nexus_node* tmp_node =
                        container_of(parent, struct nexus_node, _rb_node);
                if (key < (u64)tmp_node->start_addr)
                        new = &parent->left_child;
                else
                        new = &parent->right_child;
        }
        RB_Link_Node(&node->_rb_node, parent, new);
        RB_SolveDoubleRed(&node->_rb_node, root);
}
static void nexus_rb_tree_remove(struct nexus_node* node, struct rb_root* root)
{
        RB_Remove(&node->_rb_node, root);
}
static struct nexus_node* nexus_rb_tree_search(struct rb_root* root,
                                               vaddr start_addr)
{
        struct rb_node* node = root->rb_root;
        while (node) {
                struct nexus_node* tmp_node =
                        container_of(node, struct nexus_node, _rb_node);
                if (start_addr < tmp_node->start_addr) {
                        node = node->left_child;
                } else if (start_addr > tmp_node->start_addr) {
                        node = node->right_child;
                } else {
                        return tmp_node;
                }
        }
        return NULL;
}
static void nexus_init_manage_page(vaddr vpage_addr,
                                   struct nexus_node* nexus_root)
{
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;
        /*init the node 0 point to this page*/
        n_node->start_addr = vpage_addr;
        n_node->size = 1;
        n_node->page_left_node = NEXUS_PER_PAGE - 1;
        /*init the list*/
        INIT_LIST_HEAD(&n_node->_free_list);
        for (int i = 1; i < NEXUS_PER_PAGE; i++) {
                list_add_head(&n_node[i]._free_list, &n_node->_free_list);
        }
        /*insert to rb tree*/
        nexus_rb_tree_insert(n_node, &nexus_root->_rb_root);
}
/*return a nexus root node*/
struct nexus_node* init_nexus(struct pmm* pmm)
{
        paddr vspace_root = get_current_kernel_vspace_root();
        /*get a phy page*/
        int nexus_init_page = pmm->pmm_alloc(1, ZONE_NORMAL);
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
                (struct pmm*)pmm)) {
                pr_error("[ NEXUS ] ERROR: map error\n");
                return NULL;
        }
        /*remember clean this page*/
        memset((void*)vpage_addr, '\0', PAGE_SIZE);
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;

        /*init the node 1 as the root and let the nexus_root point to it*/
        struct nexus_node* root_node = &n_node[1];
        n_node[1].backup_manage_page = NULL;
        n_node[1].pmm = (struct pmm*)pmm;
        n_node[1]._rb_root.rb_root = &n_node[0]._rb_node;
        INIT_LIST_HEAD(&n_node[1].manage_free_list_head);

        nexus_init_manage_page(vpage_addr, &n_node[1]);

        n_node->page_left_node -= 1;
        list_add_head(&n_node->manage_free_list,
                      &root_node->manage_free_list_head);
        return &n_node[1];
}
static struct nexus_node* nexus_get_free_entry(struct nexus_node* root_node)
{
        /*from manage_free_list find one manage page that have free node*/
        struct list_entry* manage_free_list_node =
                &root_node->manage_free_list_head;
        struct list_entry* lp = manage_free_list_node->next;
        if (lp == manage_free_list_node) {
                /*if we have backend page,just use it*/
                void* backup_page = root_node->backup_manage_page;
                if (backup_page) {
                        lp = &((struct nexus_node*)backup_page)
                                      ->manage_free_list;
                        root_node->backup_manage_page = NULL;
                } else {
                        /*means no free manage can use, try alloc a new one*/
                        paddr vspace_root = get_current_kernel_vspace_root();
                        int nexus_new_page =
                                root_node->pmm->pmm_alloc(1, ZONE_NORMAL);
                        if (nexus_new_page <= 0) {
                                pr_error("[ NEXUS ] ERROR: init error\n");
                                return NULL;
                        }
                        vaddr vpage_addr =
                                KERNEL_PHY_TO_VIRT(PADDR(nexus_new_page));
                        int nexus_new_vpage = VPN(vpage_addr);
                        if (map(&vspace_root,
                                nexus_new_page,
                                nexus_new_vpage,
                                3,
                                root_node->pmm)) {
                                pr_error("[ NEXUS ] ERROR: map error\n");
                                return NULL;
                        }
                        memset((void*)vpage_addr, '\0', PAGE_SIZE);
                        nexus_init_manage_page(vpage_addr, root_node);
                        lp = &((struct nexus_node*)vpage_addr)->manage_free_list;
                }
                /*insert the lp to the manage page list*/
                list_add_head(lp, &root_node->manage_free_list_head);
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
        usable_manage_page->page_left_node++;
        if (usable_manage_page->page_left_node >= NEXUS_PER_PAGE) {
                pr_error(
                        "[ NEXUS ] ERROR: unexpected no more space but still try to insert\n");
                return NULL;
        }
        memset(usable_manage_entry, '\0', sizeof(struct nexus_node));
        return usable_manage_entry;
}
static void nexus_free_entry(struct nexus_node* nexus_entry,
                             struct nexus_node* nexus_root)
{
        struct nexus_node* page_manage_node =
                (struct nexus_node*)ROUND_DOWN((vaddr)nexus_entry, PAGE_SIZE);
        if (page_manage_node->page_left_node <= 0) {
                pr_error(
                        "[ NEXUS ] unexpect case: a manage page have no entry to free but still try free it\n");
                return;
        }
        list_add_head(&nexus_entry->_free_list, &page_manage_node->_free_list);
        page_manage_node->page_left_node--;
        if (!page_manage_node->page_left_node) {
                /*after this del, this manage page is empty*/
                if (!nexus_root->backup_manage_page) {
                        /*if no backup page, just make it as the backup page*/
                        nexus_root->backup_manage_page =
                                (void*)page_manage_node;
                } else {
                        /*else free this manage page*/
                        paddr vspace_root = get_current_kernel_vspace_root();
                        int ppn = PPN(
                                KERNEL_VIRT_TO_PHY((vaddr)page_manage_node));
                        if (unmap(vspace_root, VPN((vaddr)page_manage_node))) {
                                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                                return;
                        }
                        nexus_root->pmm->pmm_free(ppn, 1);
                        /*we also need to del it from the list and the rb tree*/
                        list_del_init(&page_manage_node->manage_free_list);
                        nexus_rb_tree_remove(page_manage_node,
                                             &nexus_root->_rb_root);
                }
        }
}
error_t get_free_page(int page_num, enum zone_type memory_zone,
                      struct nexus_node* nexus_root)
{
        /*first check the input parameter*/
        if (page_num < 0 || page_num > MIDDLE_PAGE_SIZE / PAGE_SIZE
            || memory_zone < 0 || memory_zone > ZONE_NR_MAX) {
                pr_error("[ NEXUS ] error input parameter\n");
                return -EINVAL;
        }
        /*try get a free entry*/
        struct nexus_node* free_nexus_entry = nexus_get_free_entry(nexus_root);
        if (!free_nexus_entry) {
                pr_error("[ NEXUS ] cannot find a new free nexus entry\n");
                return -ENOMEM;
        }
        paddr vspace_root = get_current_kernel_vspace_root();
        /*get phy pages from pmm*/
        int ppn = nexus_root->pmm->pmm_alloc(page_num, ZONE_NORMAL);
        if (ppn <= 0) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                return -ENOMEM;
        }
        vaddr free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        /*map, here remember, if alloc a 2M huge page, just map a level 2 page*/
        if (page_num > (MIDDLE_PAGE_SIZE / PAGE_SIZE) / 2) /*buddy pmm must
                                                              alloc a 2M page*/
        {
                if (map(&vspace_root,
                        ppn,
                        VPN(free_page_addr),
                        2,
                        nexus_root->pmm)) {
                        pr_error("[ NEXUS ] ERROR: map error\n");
                        return -ENOMEM;
                }
        } else {
                int error_num = -1;
                for (int i = 0, tmp_ppn = ppn; i < page_num;
                     i++, tmp_ppn += PAGE_SIZE) {
                        free_page_addr = KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                        if (map(&vspace_root,
                                tmp_ppn,
                                VPN(free_page_addr),
                                3,
                                nexus_root->pmm)) {
                                pr_error("[ NEXUS ] ERROR: map error\n");
                                error_num = i;
                        }
                }
                if (error_num != -1) {
                        /*unmap them all*/
                        for (int i = 0, tmp_ppn = ppn; i <= error_num;
                             i++, tmp_ppn += PAGE_SIZE) {
                                free_page_addr =
                                        KERNEL_PHY_TO_VIRT(PADDR(tmp_ppn));
                                if (unmap(vspace_root, VPN(free_page_addr))) {
                                        pr_error(
                                                "[ NEXUS ] ERROR: map success but unmap have error\n");
                                        return -ENOMEM;
                                }
                        }
                        return -ENOMEM;
                }
        }
        /*fill in the entry and link it*/
        free_nexus_entry->start_addr = free_page_addr;
        free_nexus_entry->size = page_num;
        nexus_rb_tree_insert(free_nexus_entry, &nexus_root->_rb_root);
        return 0;
}
error_t free_pages(void* p, struct nexus_node* nexus_root)
{
        if (!p || !nexus_root) {
                pr_error("[ ERROR ] ERROR: error input arg\n");
                return -EINVAL;
        }
        struct nexus_node* node =
                nexus_rb_tree_search(&nexus_root->_rb_root, (vaddr)p);
        if (!node) {
                pr_error("[ NEXUS ] ERROR: search the free page fail\n");
                return -EINVAL;
        }
        paddr vspace_root = get_current_kernel_vspace_root();
        u32 ppn = PPN(KERNEL_VIRT_TO_PHY(node->start_addr));
        if (unmap(vspace_root, VPN(node->start_addr))) {
                pr_error("[ NEXUS ] ERROR: unmap error!\n");
                return -ENOMEM;
        }
        nexus_root->pmm->pmm_free(ppn, node->size);
        nexus_free_entry(node, nexus_root);
        /*del from rb tree*/
        nexus_rb_tree_remove(node, &nexus_root->_rb_root);
        return 0;
}
static void nexus_rb_print(struct nexus_node* node){

}
void nexus_print(struct nexus_node* nexus_root)
{
        if (!nexus_root) {
                pr_error(
                        "[ NEXUS ] ERROR: expect a nexus root, pleas check or init one\n");
                return;
        }
        pr_info("=== [ NEXUS ] ===\n");
        pr_info("[ MANAGE PAGES ]\n");
        if (nexus_root->backup_manage_page) {
                pr_info("Backup Page: Yes 0x%x\n",
                        (vaddr)nexus_root->backup_manage_page);
        } else {
                pr_info("Backup Page: No \n");
        }
        struct list_entry* manage_page_list_entry =
                &nexus_root->manage_free_list_head;
        struct list_entry* tmp_mp = manage_page_list_entry->next;
        int manage_page_num = 0;
        while (tmp_mp != manage_page_list_entry) {
                manage_page_num++;
                struct nexus_node* manage_page = container_of(
                        tmp_mp, struct nexus_node, manage_free_list);
                pr_info("\tmanage page %d: %d record used\n",
                        manage_page_num,
                        manage_page->page_left_node);
                struct list_entry* free_entry = &manage_page->_free_list;
                struct list_entry* tmp_fe = free_entry->next;
                int free_entry_num = 0;
                while (tmp_fe != free_entry) {
                        free_entry_num++;
                        tmp_fe = tmp_fe->next;
                }
                pr_info("\tfree entry list linked number is %d\n",
                        free_entry_num);
                if (free_entry_num != manage_page->page_left_node) {
                        pr_error(
                                "[ NEXUS ] ERROR: this manage page declared free entry is unequal to counted entry\n");
                }
                tmp_mp = tmp_mp->next;
        }
        pr_info("=== TOTAL: %d ===\n", manage_page_num);
        /*then print the rb tree*/
        pr_info("[ USED PAGES ]\n");    /*include the nexus pages*/
        nexus_rb_print(nexus_root);
}