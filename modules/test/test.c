#include <modules/test/test.h>

static struct test_case all_test[MAX_TEST_CASE] = {
        {rb_tree_test, "rb_tree\0"},
        {arch_vmm_test, "arch_vmm\0"},
        {nexus_test, "nexus\0"},
		/*the pmm test will use almost all of the memory frame, so it must put
         * at the end*/
        {pmm_test, "pmm\0"},
};

void test(void)
{
        pr_info("====== [ KERNEL TEST ] ======\n");
		for(int i=0;i<MAX_TEST_CASE;i++){
			if(all_test[i].test()){
				pr_error("[ TEST ERROR ] test %s fail!\n",all_test[i].name);
				break;
			}else{
				pr_info("[ TEST PASS ] test %s pass!\n",all_test[i].name);
			}
		}
}