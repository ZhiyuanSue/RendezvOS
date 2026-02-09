#include <modules/test/test.h>

extern volatile i64 jeffies;
static struct single_test_case single_test[MAX_SINGLE_TEST_CASE] = {
        {rb_tree_test, "rb_tree\0"},
        {arch_vmm_test, "arch_vmm\0"},
        // {nexus_test, "nexus\0"},
        /* if kmalloc is ok ,then nexus must be ok*/
        {kmalloc_test, "kmalloc\0"},
        /*the pmm test will use almost all of the memory frame, so it must put
         * at the end*/
        // {pmm_test, "pmm\0"},
        {elf_read_test, "elf_read\0"},
        {task_test, "task_test\0"},
        {test_pci_scan, "test_pci_scan\0"},
        {ipc_test, "ipc\0"},
};

void single_cpu_test(void)
{
        pr_notice("====== [ KERNEL SINGLE CPU TEST ] ======\n");
        bool test_pass = true;
        for (int i = 0; i < MAX_SINGLE_TEST_CASE; i++) {
                if ((u64)(single_test[i].test)) {
                        if (single_test[i].test()) {
                                pr_error("[ TEST @%8x ] ERROR: test %s fail!\n",
                                         jeffies,
                                         single_test[i].name);
                                test_pass = false;
                                break;
                        } else {
                                pr_notice("[ TEST @%8x ] PASS: test %s ok!\n",
                                          jeffies,
                                          single_test[i].name);
                        }
                }
        }
        if (test_pass)
                pr_notice("====== [ SINGLE CPU TEST PASS ] ======\n");
}