#include <rendezvos/task/tcb.h>
#include <rendezvos/error.h>
#include <modules/log/log.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/task/initcall.h>
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
char idle_thread_name[] = "idle_thread";
void* idle_thread(void* arg)
{
        (void)arg;
        while (1) {
                /*TODO:might close the int*/
                schedule(percpu(core_tm));
        }
}
void idle_thread_init(void)
{
        error_t e = gen_thread_from_func(&percpu(idle_thread_ptr),
                                         idle_thread,
                                         idle_thread_name,
                                         percpu(core_tm),
                                         NULL);
        if (e) {
                pr_error("[ Error ]idle thread init fail\n");
        }
}
DEFINE_INIT(idle_thread_init);