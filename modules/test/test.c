#include <modules/test/test.h>

void test(void) {
    pr_info("start test\n");
    pmm_test();
    arch_vmm_test();
}