#include <modules/test/test.h>
#include <rendezvos/task/tcb.h>

char test_thread_name[] = "test_thread";
void BSP_test()
{
#ifdef TEST
        single_cpu_test();
        multi_cpu_test();
#endif
        schedule(percpu(core_tm));
}
void AP_test()
{
#ifdef TEST
        multi_cpu_test();
#endif
        schedule(percpu(core_tm));
}
error_t create_test_thread(bool is_bsp_test)
{
        Thread_Base* test_t;
        if (is_bsp_test)
                test_t = create_thread((void*)BSP_test, 0);
        else
                test_t = create_thread((void*)AP_test, 0);
        if (!test_t) {
                pr_error("[Error] create test thread fail\n");
                return -E_RENDEZVOS;
        }
        thread_set_name(test_thread_name, test_t);
        error_t e = thread_join(percpu(core_tm)->current_task,
                                test_t); /*current task must be root task*/
        return e;
}