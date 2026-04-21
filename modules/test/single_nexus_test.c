// #define DEBUG
#include <modules/test/test.h>
#include <rendezvos/mm/nexus.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/task/id.h>
#define NR_MAX_TEST NEXUS_PER_PAGE * 3
void* test_ptrs[NR_MAX_TEST];
void nexus_print(struct nexus_node* nexus_root)
{
        if (!nexus_root) {
                pr_error(
                        "[ NEXUS ] ERROR: expect a nexus root, pleas check or init one\n");
                return;
        }
        debug("=== [ NEXUS ] ===\n");
        debug("[ MANAGE PAGES ]\n");
        struct list_entry* manage_page_list_entry =
                &nexus_root->manage_free_list;
        struct list_entry* tmp_mp = manage_page_list_entry->next;
        int manage_page_num = 0;
        while (tmp_mp != manage_page_list_entry) {
                manage_page_num++;
                struct nexus_node* manage_page = container_of(
                        tmp_mp, struct nexus_node, manage_free_list);
                debug("\t[ NEXUS ] have empty entry manage page %d with addr 0x%lx: %d record can use\n",
                      manage_page_num,
                      (vaddr)manage_page,
                      manage_page->page_left_nexus);
                struct list_entry* free_entry = &manage_page->aux_list;
                struct list_entry* tmp_fe = free_entry->next;
                u64 free_entry_num = 0;
                while (tmp_fe != free_entry) {
                        free_entry_num++;
                        tmp_fe = tmp_fe->next;
                }
                if (free_entry_num != manage_page->page_left_nexus) {
                        pr_error(
                                "[ NEXUS ] ERROR: this manage page declared free entry is unequal to counted entry\n");
                }
                tmp_mp = tmp_mp->next;
        }
        debug("=== TOTAL: %d ===\n", manage_page_num);
        /*then print the rb tree*/
        debug("[ USED PAGES ]\n"); /*include the nexus pages*/
        struct rb_node* tmp_rb = nexus_root->_rb_root.rb_root;
        if (!tmp_rb) {
                pr_error("\t[ NEXUS ] No used page\n");
                return;
        }
        while (tmp_rb->left_child) {
                tmp_rb = tmp_rb->left_child;
        }
        while (tmp_rb) {
                struct nexus_node* tmp_node =
                        container_of(tmp_rb, struct nexus_node, _rb_node);
                if (tmp_node)
                        debug("\t[ NEXUS ] Used page: start vaddr 0x%lx, size 0x%lx\n",
                              tmp_node->v_region.addr,
                              tmp_node->v_region.len);
                tmp_rb = RB_Next(tmp_rb);
        }
        debug("=== [ END ] ===\n")
}
int nexus_test(void)
{
        debug("sizeof struct nexus_node is 0x%lx\n", sizeof(struct nexus_node));
        /*after the nexus init, we try to print it first*/
        nexus_print(percpu(nexus_root));
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                int page_num = 2;
                if (i % 2)
                        page_num = MIDDLE_PAGES;
                test_ptrs[i] = get_free_page(page_num,
                                             KERNEL_VIRT_OFFSET,
                                             percpu(nexus_root),
                                             0,
                                             PAGE_ENTRY_NONE);
                if (test_ptrs[i]) {
                        *((u64*)(test_ptrs[i])) = 0;
                        *((u64*)(test_ptrs[i] + PAGE_SIZE)) = 0;
                }
        }
        nexus_print(percpu(nexus_root));
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && i % 2) {
                        error_t ret = free_pages(test_ptrs[i],
                                                 MIDDLE_PAGES,
                                                 0,
                                                 percpu(nexus_root));
                        if (ret != REND_SUCCESS) {
                                pr_error(
                                        "[TEST] Failed to free pages: ret=%d\n",
                                        ret);
                                return -E_REND_TEST;
                        }
                }
        }
        nexus_print(percpu(nexus_root));
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && !(i % 2)) {
                        error_t ret = free_pages(
                                test_ptrs[i], 2, 0, percpu(nexus_root));
                        if (ret != REND_SUCCESS) {
                                pr_error(
                                        "[TEST] Failed to free pages: ret=%d\n",
                                        ret);
                                return -E_REND_TEST;
                        }
                }
        }
        nexus_print(percpu(nexus_root));

        /*try to add a new vs and then use the new vs test user*/
        /*add a new vspace*/
        error_t e = 0;
        VS_Common* vs = new_vspace();
        if (!vs) {
                e = -E_REND_TEST;
                goto nexus_test_fail;
        }
        paddr new_vs_paddr = new_vs_root(0, &percpu(Map_Handler));
        if (!new_vs_paddr) {
                e = -E_REND_TEST;
                goto nexus_test_fail;
        }
        set_vspace_root_addr(vs, new_vs_paddr);
        struct nexus_node* new_vs_nexus_root =
                nexus_create_vspace_root_node(percpu(nexus_root), vs);
        init_vspace(vs, get_new_id(&pid_manager), new_vs_nexus_root);

        /*start new vspace nexus test*/
        vaddr start_test_addr = PAGE_SIZE;
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                int page_num = 2;
                if (i % 2) {
                        page_num = MIDDLE_PAGES;
                        start_test_addr =
                                ROUND_UP(start_test_addr, MIDDLE_PAGE_SIZE);
                }
                test_ptrs[i] = get_free_page(page_num,
                                             start_test_addr,
                                             percpu(nexus_root),
                                             vs,
                                             PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                                                     | PAGE_ENTRY_WRITE);
                start_test_addr += page_num * PAGE_SIZE;
                if (test_ptrs[i]) {
                        *((u64*)(test_ptrs[i])) = 0;
                        *((u64*)(test_ptrs[i] + PAGE_SIZE)) = 0;
                }
        }
        nexus_print(percpu(nexus_root));
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && i % 2) {
                        error_t ret = free_pages(test_ptrs[i],
                                                 MIDDLE_PAGES,
                                                 vs,
                                                 percpu(nexus_root));
                        if (ret != REND_SUCCESS) {
                                pr_error(
                                        "[TEST] Failed to free pages: ret=%d\n",
                                        ret);
                                return -E_REND_TEST;
                        }
                }
        }
        nexus_print(percpu(nexus_root));
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && !(i % 2)) {
                        error_t ret = free_pages(
                                test_ptrs[i], 2, vs, percpu(nexus_root));
                        if (ret != REND_SUCCESS) {
                                pr_error(
                                        "[TEST] Failed to free pages: ret=%d\n",
                                        ret);
                                return -E_REND_TEST;
                        }
                }
        }

        /* range flags update sanity (user 4K mappings) */
        void* p4 = get_free_page(4,
                                 start_test_addr,
                                 percpu(nexus_root),
                                 vs,
                                 PAGE_ENTRY_READ | PAGE_ENTRY_VALID);
        if (!p4)
                return -E_REND_TEST;
        error_t pe = nexus_update_range_flags(percpu(nexus_root),
                                              vs,
                                              (vaddr)p4 + PAGE_SIZE,
                                              2 * PAGE_SIZE,
                                              NEXUS_RANGE_FLAGS_ABSOLUTE,
                                              PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                                                      | PAGE_ENTRY_WRITE,
                                              0);
        if (pe != REND_SUCCESS)
                return -E_REND_TEST;
        ENTRY_FLAGS_t f1 = 0, f2 = 0;
        int level1 = 0, level2 = 0;
        if (have_mapped(vs,
                        VPN((vaddr)p4 + PAGE_SIZE),
                        &f1,
                        &level1,
                        &percpu(Map_Handler))
                    <= 0
            || have_mapped(vs,
                           VPN((vaddr)p4 + 2 * PAGE_SIZE),
                           &f2,
                           &level2,
                           &percpu(Map_Handler))
                       <= 0)
                return -E_REND_TEST;
        if (level1 != 3 || level2 != 3)
                return -E_REND_TEST;
        if ((f1 & PAGE_ENTRY_WRITE) == 0 || (f2 & PAGE_ENTRY_WRITE) == 0)
                return -E_REND_TEST;
        (void)free_pages(p4, 4, vs, percpu(nexus_root));

        if (vs) {
                if (vs_common_is_table_vspace(vs) && vs != &root_vspace) {
                        ref_put(&vs->refcount, free_vspace_ref);
                }
                vs = NULL;
        }

        return REND_SUCCESS;
nexus_test_fail:
        return e;
}