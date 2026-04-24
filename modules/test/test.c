#include <modules/test/test.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/system/powerd.h>
#include <common/atomic.h>

void* BSP_test(void* arg)
{
        (void)arg;
#ifdef RENDEZVOS_TEST
        core_test_phase_set(CORE_TEST_PHASE_CORE_TESTS);
        single_cpu_test();
        multi_cpu_test();
        core_test_phase_set(CORE_TEST_PHASE_UPPER_TESTS);

        pr_info("[Core test] Waiting for other tests to complete\n");
        while (core_test_phase_get() < CORE_TEST_PHASE_DONE) {
                schedule(percpu(core_tm));
        }

#ifdef RENDEZVOS_CORE_AUTO_POWEROFF
        pr_info("[Core test] All tests completed, requesting shutdown\n");
        (void)rendezvos_request_poweroff();
#endif
#endif
        thread_set_status(percpu(init_thread_ptr), thread_status_ready);
        schedule(percpu(core_tm));
        return NULL;
}
void* AP_test(void* arg)
{
        (void)arg;
#ifdef RENDEZVOS_TEST
        multi_cpu_test();
#endif
        thread_set_status(percpu(init_thread_ptr), thread_status_ready);
        schedule(percpu(core_tm));
        return NULL;
}
char test_thread_name[] = "test_thread";
error_t create_test_thread(bool is_bsp_test)
{
        error_t e;
        if (is_bsp_test) {
                e = gen_thread_from_func(NULL,
                                         BSP_test,
                                         test_thread_name,
                                         percpu(core_tm),
                                         NULL);
        } else {
                e = gen_thread_from_func(
                        NULL, AP_test, test_thread_name, percpu(core_tm), NULL);
        }
        return e;
}

static atomic64_t g_core_test_phase = {.counter = CORE_TEST_PHASE_BOOT};

core_test_phase_t core_test_phase_get(void)
{
        return (core_test_phase_t)atomic64_load(
                (volatile const u64*)&g_core_test_phase.counter);
}

void core_test_phase_set(core_test_phase_t phase)
{
        atomic64_store((volatile u64*)&g_core_test_phase.counter, (u64)phase);
}