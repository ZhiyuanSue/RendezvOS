#include <modules/test/test.h>
#include <rendezvos/percpu.h>
#include <rendezvos/limits.h>

extern int BSP_ID;
extern int NR_CPU;
extern volatile i64 jeffies;
static struct smp_test_case smp_test[MAX_SMP_TEST_CASE] = {
        {smp_lock_test, "smp spin_lock", smp_lock_check},
};
enum multi_cpu_test_state {
        multi_cpu_test_not_start,
        multi_cpu_test_running,
        multi_cpu_test_success,
        multi_cpu_test_fail
};
volatile enum multi_cpu_test_state test_state[RENDEZVOS_MAX_CPU_NUMBER];
volatile bool last_test_finished = true;
void multi_cpu_test(void)
{
        int cpu_id = percpu(cpu_number);
        bool test_pass = true;
        if (cpu_id == BSP_ID)
                pr_info("====== [ KERNEL MULTI CPU TEST ] ======\n");
        for (int i = 0; i < MAX_SMP_TEST_CASE; i++) {
                if (!(u64)(smp_test[i].test))
                        break;
                if (cpu_id == BSP_ID)
                        pr_info("[ MULTI CPU TEST %s ]\n", smp_test[i].name);
                test_state[cpu_id] = multi_cpu_test_running;
                /*wait for other cpu start test*/
                while (last_test_finished) {
                        bool have_cpu_not_start = false;
                        for (int j = 0; j < NR_CPU; j++) {
                                if (test_state[j] == multi_cpu_test_not_start) {
                                        have_cpu_not_start = true;
                                        break;
                                }
                        }
                        if (!have_cpu_not_start)
                                break;
                }
                if (cpu_id == BSP_ID)
                        last_test_finished = false;
                if (!smp_test[i].check_result) {
                        /* if no cpu 0 check function, just run it on every core
                         * and check the return value */
                        if (smp_test[i].test()) {
                                test_state[cpu_id] = multi_cpu_test_fail;
                        } else {
                                test_state[cpu_id] = multi_cpu_test_success;
                        }
                } else {
                        /* if some check function exist, we just run them on
                         * every core */
                        smp_test[i].test();
                        test_state[cpu_id] = multi_cpu_test_success;
                }

                /*finish this test and sync*/
                if (cpu_id == BSP_ID) {
                        bool all_test_success = true;
                        /*wait for other cpu finish*/
                        while (1) {
                                bool have_cpu_not_finish = false;
                                for (int j = 0; j < NR_CPU; j++) {
                                        if (test_state[j]
                                            == multi_cpu_test_running) {
                                                have_cpu_not_finish = true;
                                                break;
                                        }
                                }
                                if (!have_cpu_not_finish)
                                        break;
                        }
                        if (!smp_test[i].check_result) {
                                for (int j = 0; j < NR_CPU; j++) {
                                        if (test_state[j]
                                            == multi_cpu_test_fail) {
                                                all_test_success = false;
                                                pr_error(
                                                        "[ TEST @%8x ] ERROR: test %s on cpu: %d fail!\n",
                                                        jeffies,
                                                        smp_test[i].name,
                                                        cpu_id);
                                                break;
                                        }
                                }
                        } else {
                                all_test_success = smp_test[i].check_result();
                        }

                        if (!all_test_success) {
                                test_pass = false;
                                break;
                        }
                        pr_info("[ TEST @%8x ] PASS: test %s ok!\n",
                                jeffies,
                                smp_test[i].name);
                        last_test_finished = true;
                        for (int j = 0; j < NR_CPU; j++)
                                test_state[j] = multi_cpu_test_not_start;
                } else {
                        while (!last_test_finished)
                                ;
                }
        }
        if (percpu(cpu_number) == BSP_ID) {
                if (test_pass)
                        pr_info("====== [ MULTI CPU TEST PASS ] ======\n");
        }
}