#include <shampoos/mm/nexus.h>
#include <shampoos/mm/vmm.h>
#include <common/string.h>
#include <modules/log/log.h>
#include <shampoos/error.h>

struct rb_root* nexus_root;

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
static void nexus_init_manager_page(vaddr vpage_addr)
{
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;
        /*init the node 0 point to this page*/
        n_node->start_addr = vpage_addr;
        n_node->size = 1;
        n_node->page_left_node = NEXUS_PER_PAGE - 1;
        /*init the list*/
        INIT_LIST_HEAD(&n_node->_free_list);
        for(int i=1;i<NEXUS_PER_PAGE;i++){
                list_add_head(&n_node[i]._free_list,&n_node->_free_list);
        }
        /*insert to rb tree*/
        nexus_rb_tree_insert(n_node, nexus_root);
}
error_t init_nexus(struct pmm* pmm)
{       
        paddr vspace_root = get_current_kernel_vspace_root();
        /*get a phy page*/
        int nexus_init_page = pmm->pmm_alloc(1, ZONE_NORMAL);
        if (nexus_init_page <= 0) {
                pr_error("[ NEXUS ] ERROR: init error\n");
                return -ENOMEM;
        }
        /*get a vir page with Identical mapping*/
        vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_init_page));
        if (map(&vspace_root,
                nexus_init_page,
                VPN(vpage_addr),
                3,
                (struct pmm*)pmm)) {
                pr_error("[ NEXUS ] ERROR: map error\n");
                return -ENOMEM;
        }
        /*remember clean this page*/
        memset((void*)vpage_addr, '\0', PAGE_SIZE);
        struct nexus_node* n_node = (struct nexus_node*)vpage_addr;

        /*init the node 1 as the root and let the nexus_root point to it*/
        nexus_root = &n_node[1]._rb_root;
        struct nexus_node* root_node = &n_node[1];
        n_node[1].backup_manage_page = NULL;
        n_node[1].pmm = (struct pmm*)pmm;
        n_node[1]._rb_root.rb_root = &n_node[0]._rb_node;
        INIT_LIST_HEAD(&n_node[1].manage_free_list_head);

        nexus_init_manager_page(vpage_addr);

        n_node->page_left_node -= 1;
        list_add_head(&n_node->manage_free_list, &root_node->manage_free_list_head);
        return 0;
}
static struct nexus_node* nexus_get_free_entry()
{
        /*from manage_free_list find one manage page that have free node*/
        struct nexus_node* root_node =
                container_of(nexus_root, struct nexus_node, _rb_root);
        struct list_entry* manage_free_list_node =
                &root_node->manage_free_list_head;
        struct list_entry* lp = manage_free_list_node->next;
        if (lp == manage_free_list_node) {
                /*if we have backend page,just use it*/
                void* backup_page = root_node->backup_manage_page;
                if(backup_page){
                        lp=&((struct nexus_node*)backup_page)->manage_free_list;
                        root_node->backup_manage_page=NULL;
                }
                else{
                        /*means no free manage can use, try alloc a new one*/
                        paddr vspace_root = get_current_kernel_vspace_root();
                        int nexus_new_page= root_node->pmm->pmm_alloc(1,ZONE_NORMAL);
                        if(nexus_new_page<=0)
                        {
                                pr_error("[ NEXUS ] ERROR: init error\n");
                                return NULL;
                        }
                        vaddr vpage_addr = KERNEL_PHY_TO_VIRT(PADDR(nexus_new_page));
                        int nexus_new_vpage = VPN(vpage_addr);
                        if(map(&vspace_root,nexus_new_page,nexus_new_vpage,3,root_node->pmm)){
                                pr_error("[ NEXUS ] ERROR: map error\n");
                                return NULL;
                        }
                        memset((void*)vpage_addr,'\0',PAGE_SIZE);
                        nexus_init_manager_page(vpage_addr);
                        lp = &((struct nexus_node*)vpage_addr)->manage_free_list;
                }
                /*insert the lp to the manage page list*/
                list_add_head(lp,&root_node->manage_free_list_head);
        }
        if(!lp || lp == manage_free_list_node){
                pr_error("[ ERROR ]find an free manage page fail\n");
                return NULL;
        }
        /*here we promise we have a usable lp, then from lp get the manage page mate info*/
        struct nexus_node* usable_manage_page = container_of(lp,struct nexus_node,manage_free_list);
        /*use the free list to find a new entry*/
        struct list_entry* usable_entry = usable_manage_page->_free_list.next;
        struct nexus_node* usable_manage_entry = container_of(usable_entry,struct nexus_node,_free_list);
        /*clean the entry,remember that we must del it first then clean the entry*/
        list_del_init(usable_entry);
        memset(usable_manage_entry,'\0',sizeof(struct nexus_node));
        return usable_manage_entry;
}
error_t get_free_page(int order, enum zone_type memory_zone)
{
        /*first check the input parameter*/
        if (order < 0 || order > 10 || memory_zone < 0
            || memory_zone > ZONE_NR_MAX) {
                pr_error("[ NEXUS ] error input parameter\n");
                return -EINVAL;
        }
        /*try get a free entry*/
        struct nexus_node* free_nexus_entry = nexus_get_free_entry();
        if (!free_nexus_entry) {
                pr_error("[ NEXUS ] cannot find a new free nexus entry\n");
                return -ENOMEM;
        }
        /*get phy pages from pmm*/

        /*map*/

        /*fill in the entry and link it*/
}
error_t free_pages(void* p)
{
}