
// #define DEBUG
#include <modules/test/test.h>
#include <rendezvos/mm/nexus.h>
#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#define NR_MAX_TEST NEXUS_PER_PAGE * 80
DEFINE_PER_CPU(void*, smp_test_ptrs[NR_MAX_TEST]);
bool smp_check_rb(struct rb_node* node, int* height, int* count, int level)
{
        for (int i = 0; i < level; i++)
                debug("\t");

        if (node == NULL) {
                debug("[NULL]\n");
                *height = 0;
                return true;
        }
        debug("[Node:%x][Color:%d] ", node, RB_COLOR(node));
        if (RB_PARENT(node)) {
                debug(" [Parent:%x]\n", RB_PARENT(node));
        } else {
                debug("\n");
        }

        int left_height, right_height;
        debug("[L]");
        bool l = smp_check_rb(node->left_child, &left_height, count, level + 1);
        debug("[R]");
        bool r = smp_check_rb(
                node->right_child, &right_height, count, level + 1);

        /*update the height*/
        (*height) = RB_COLOR(node) ? (left_height + 1) : left_height;
        (*count)++;

        if (!l || !r) {
                pr_error("l is %d and r is %d\n", l, r);
                return false;
        }

        /*check height*/
        if (left_height != right_height) {
                pr_error("height is unequal ,left %d,right %d\n",
                         left_height,
                         right_height);
                return false;
        }

        /*check color*/
        if ((RB_COLOR(node) == RB_RED)
            && (!RB_PARENT(node) || RB_COLOR(RB_PARENT(node)) == RB_RED)) {
                pr_error("double red error\n");
                return false;
        }

        return true;
}
int smp_nexus_test(void)
{
        debug("sizeof struct nexus_node is 0x%x\n", sizeof(struct nexus_node));

        // if (percpu(cpu_number) == 0) {
        //         int height;
        //         int count = 0;
        //         smp_check_rb((percpu(nexus_root)->_rb_root.rb_root),
        //                      &height,
        //                      &count,
        //                      0);
        // }

        /*after the nexus init, we try to print it first*/
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                int page_num = 2;
                percpu(smp_test_ptrs)[i] = get_free_page(page_num,
                                                         KERNEL_VIRT_OFFSET,
                                                         percpu(nexus_root),
                                                         0,
                                                         PAGE_ENTRY_NONE);
                if (percpu(smp_test_ptrs)[i]) {
                        *((u64*)(percpu(smp_test_ptrs)[i])) = 0;
                        *((u64*)(percpu(smp_test_ptrs)[i] + PAGE_SIZE)) = 0;
                }
        }

        // if (percpu(cpu_number) == 0) {
        //         int height;
        //         int count = 0;
        //         if (smp_check_rb((percpu(nexus_root)->_rb_root.rb_root),
        //                          &height,
        //                          &count,
        //                          0)) {
        //                 pr_info("pass smp rb check\n");
        //         } else {
        //                 pr_error("smp rb check fail\n");
        //         }
        // }
        for (u64 i = 0; i < NR_MAX_TEST; i++) {
                if (percpu(smp_test_ptrs)[i])
                        free_pages(percpu(smp_test_ptrs)[i],
                                   2,
                                   0,
                                   percpu(nexus_root));
        }
        return 0;
}