#include <modules/test/test.h>

void test(void)
{
        pr_info("====== [ TEST ] ======\n");
        rb_tree_test();
        arch_vmm_test();
        /*the pmm test will use almost all of the memory frame, so it must put
         * at the end*/
        pmm_test();
}