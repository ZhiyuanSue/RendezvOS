#include <modules/test/test.h>

extern volatile i64 jeffies;
static struct test_case smp_test[MAX_SMP_TEST_CASE] = {

};
void multi_cpu_test(void)
{
        // pr_info("====== [ KERNEL MULTI CPU TEST ] ======\n");
        bool test_pass = true;
        for (int i = 0; i < MAX_SINGLE_TEST_CASE; i++) {
                if ((u64)(smp_test[i].test)) {
                        if (smp_test[i].test()) {
                                pr_error("[ TEST @%8x ] ERROR: test %s fail!\n",
                                         jeffies,
                                         smp_test[i].name);
                                test_pass = false;
                                break;
                        } else {
                                pr_info("[ TEST @%8x ] PASS: test %s ok!\n",
                                        jeffies,
                                        smp_test[i].name);
                        }
                }
        }
        if (test_pass){

			// pr_info("====== [ MULTI CPU TEST PASS ] ======\n");
		}
}