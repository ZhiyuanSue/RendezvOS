#include <modules/test/test.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/system/powerd.h>

void* BSP_test(void* arg)
{
        (void)arg;
#ifdef RENDEZVOS_TEST
        single_cpu_test();
        multi_cpu_test();
#ifdef RENDEZVOS_CORE_AUTO_POWEROFF
        pr_info("[Core test]core test over and shutdown\n");
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