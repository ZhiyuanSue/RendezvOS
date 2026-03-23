#include <modules/test/test.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/limits.h>

extern cpu_id_t BSP_ID;
extern int NR_CPU;
extern volatile i64 jeffies;
static struct smp_test_case smp_test[MAX_SMP_TEST_CASE] = {
        /* CPU support:
         * - scalable: works for any NR_CPU >= 1
         * - even-only: requires paired roles, uses active-even CPUs if NR_CPU
         * is odd
         * - fixed-4: designed specifically for NR_CPU == 4 (otherwise
         * skipped/pass)
         */
        /* scalable */
        {smp_lock_test, "smp spin_lock", smp_lock_check},
        // {smp_log_test, "smp log test", smp_log_check}, // just see the output
        /* scalable */
        {smp_nexus_test, "smp nexus test", NULL},
        /* scalable */
        {smp_kmalloc_test, "smp spmalloc", NULL},
        /* even-only (skips CPUs >= active-even) */
        {smp_ms_queue_test, "smp ms queue test", smp_ms_queue_check},
        /* even-only (skips CPUs >= active-even) */
        {smp_ms_queue_dyn_alloc_test,
         "smp ms queue dyn alloc test",
         smp_ms_queue_dyn_alloc_check},
        /* fixed-4 (skipped when NR_CPU != 4) */
        {smp_ms_queue_check_test,
         "smp ms queue check test",
         smp_ms_queue_check_test_check},
        {
                /* even-only (skips CPUs >= active-even) */
                smp_ipc_test,
                "smp ipc test",
                NULL,
        },
        {
                /* scalable */
                smp_port_robustness_test,
                "smp port robustness test",
                NULL,
        }};
enum multi_cpu_test_state {
        multi_cpu_test_not_start,
        multi_cpu_test_running,
        multi_cpu_test_success,
        multi_cpu_test_fail
};
volatile enum multi_cpu_test_state test_state[RENDEZVOS_MAX_CPU_NUMBER];
volatile u64 curr_test = 0;
void multi_cpu_test(void)
{
        cpu_id_t cpu_id = percpu(cpu_number);
        bool test_pass = true;
        if (cpu_id == BSP_ID)
                pr_notice("====== [ KERNEL MULTI CPU TEST ] ======\n");
        for (u64 i = 0; i < MAX_SMP_TEST_CASE; i++) {
                if (!(u64)(smp_test[i].test))
                        break;
                if (cpu_id == BSP_ID)
                        pr_notice("[ MULTI CPU TEST %s ]\n", smp_test[i].name);
                test_state[cpu_id] = multi_cpu_test_running;
                /*wait for other cpu start test*/
                while (curr_test == i) {
                        bool have_cpu_not_start = false;
                        for (int j = 0; j < NR_CPU; j++) {
                                if (test_state[j] == multi_cpu_test_not_start) {
                                        have_cpu_not_start = true;
                                        break;
                                }
                        }
                        arch_cpu_relax();
                        if (!have_cpu_not_start)
                                break;
                }
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
                                arch_cpu_relax();
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
                        pr_notice("[ TEST @%8x ] PASS: test %s ok!\n",
                                  jeffies,
                                  smp_test[i].name);
                        for (int j = 0; j < NR_CPU; j++)
                                test_state[j] = multi_cpu_test_not_start;
                        curr_test++;
                } else {
                        while (curr_test == i) {
                                arch_cpu_relax();
                        }
                }
        }
        if (percpu(cpu_number) == BSP_ID) {
                if (test_pass)
                        pr_notice("====== [ MULTI CPU TEST PASS ] ======\n");
        }
}