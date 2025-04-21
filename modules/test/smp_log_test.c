#include <modules/test/test.h>
#include <modules/log/log.h>

#define SMP_LOG_TEST_ROUND 10000
int smp_log_test(void)
{
        for (int i = 0; i < SMP_LOG_TEST_ROUND; i++) {
                pr_info("smp test");
        }
        return 0;
}
int smp_log_check(void)
{
        pr_info("\n");
        return 0;
}