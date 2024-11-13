#include <modules/test/test.h>

static struct test_case all_test[MAX_TEST_CASE] = {
        {rb_tree_test, "rb_tree\0"},
        {arch_vmm_test, "arch_vmm\0"},
        // {nexus_test, "nexus\0"},
        /* if spmalloc is ok ,then nexus must be ok*/
        {spmalloc_test, "spmalloc\0"},
        /*the pmm test will use almost all of the memory frame, so it must put
         * at the end*/
        // {pmm_test, "pmm\0"},
};

void test(void)
{
        pr_info("====== [ KERNEL TEST ] ======\n");
        bool test_pass = true;
        for (int i = 0; i < MAX_TEST_CASE; i++) {
                if ((u64)(all_test[i].test)) {
                        if (all_test[i].test()) {
                                pr_error("[ TEST ] ERROR: test %s fail!\n",
                                         all_test[i].name);
                                test_pass = false;
                                break;
                        } else {
                                pr_info("[ TEST ] PASS: test %s ok!\n",
                                        all_test[i].name);
                        }
                }
        }
        if (test_pass)
                pr_info("====== [ TEST PASS ] ======\n");
}