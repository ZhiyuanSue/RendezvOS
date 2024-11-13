// #define DEBUG
#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#define NR_MAX_TEST NEXUS_PER_PAGE * 3
extern struct nexus_node* nexus_root;
void* test_ptrs[NR_MAX_TEST];
// void nexus_print(struct nexus_node* nexus_root)
// {
//         if (!nexus_root) {
//                 pr_error(
//                         "[ NEXUS ] ERROR: expect a nexus root, pleas check or init one\n");
//                 return;
//         }
//         debug("=== [ NEXUS ] ===\n");
//         debug("[ MANAGE PAGES ]\n");
//         if (nexus_root->backup_manage_page) {
//                 debug("Backup Page: Yes 0x%x\n",
//                       (vaddr)nexus_root->backup_manage_page);
//         } else {
//                 debug("Backup Page: No \n");
//         }
//         struct list_entry* manage_page_list_entry =
//                 &nexus_root->manage_free_list;
//         struct list_entry* tmp_mp = manage_page_list_entry->next;
//         int manage_page_num = 0;
//         while (tmp_mp != manage_page_list_entry) {
//                 manage_page_num++;
//                 struct nexus_node* manage_page = container_of(
//                         tmp_mp, struct nexus_node, manage_free_list);
//                 debug("\t[ NEXUS ] have empty entry manage page %d with addr 0x%x: %d record can use\n",
//                       manage_page_num,
//                       (vaddr)manage_page,
//                       manage_page->page_left_nexus);
//                 struct list_entry* free_entry = &manage_page->_free_list;
//                 struct list_entry* tmp_fe = free_entry->next;
//                 int free_entry_num = 0;
//                 while (tmp_fe != free_entry) {
//                         free_entry_num++;
//                         tmp_fe = tmp_fe->next;
//                 }
//                 if (free_entry_num != manage_page->page_left_nexus) {
//                         pr_error(
//                                 "[ NEXUS ] ERROR: this manage page declared free entry is unequal to counted entry\n");
//                 }
//                 tmp_mp = tmp_mp->next;
//         }
//         debug("=== TOTAL: %d ===\n", manage_page_num);
//         /*then print the rb tree*/
//         debug("[ USED PAGES ]\n"); /*include the nexus pages*/
//         struct rb_node* tmp_rb = nexus_root->_rb_root.rb_root;
//         while (tmp_rb->left_child) {
//                 tmp_rb = tmp_rb->left_child;
//         }
//         while (tmp_rb) {
//                 struct nexus_node* tmp_node =
//                         container_of(tmp_rb, struct nexus_node, _rb_node);
//                 if (tmp_node)
//                         debug("\t[ NEXUS ] Used page: start vaddr 0x%x, size 0x%x\n",
//                               tmp_node->start_addr,
//                               tmp_node->size);
//                 tmp_rb = RB_Next(tmp_rb);
//         }
//         debug("=== [ END ] ===\n")
// }
int nexus_test(void)
{
        debug("sizeof struct nexus_node is 0x%x\n", sizeof(struct nexus_node));
        /*after the nexus init, we try to print it first*/
        nexus_print(nexus_root);
        for (int i = 0; i < NR_MAX_TEST; i++) {
                int page_num = 2;
                if (i % 2)
                        page_num = MIDDLE_PAGES;
                test_ptrs[i] = get_free_page(
                        page_num, ZONE_NORMAL, KERNEL_VIRT_OFFSET, nexus_root);
                if (test_ptrs[i]) {
                        *((u64*)(test_ptrs[i])) = 0;
                        *((u64*)(test_ptrs[i] + PAGE_SIZE)) = 0;
                }
        }
        nexus_print(nexus_root);
        for (int i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && i % 2)
                        free_pages(test_ptrs[i], MIDDLE_PAGES, nexus_root);
        }
        nexus_print(nexus_root);
        for (int i = 0; i < NR_MAX_TEST; i++) {
                if (test_ptrs[i] && !(i % 2))
                        free_pages(test_ptrs[i], 2, nexus_root);
        }
        nexus_print(nexus_root);
        return 0;
}