#include <modules/test/test.h>

void test(void)
{
        pr_info("====== [ TEST ] ======\n");
        pmm_test();
        rb_tree_test();
        arch_vmm_test();
}