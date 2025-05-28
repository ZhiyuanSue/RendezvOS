#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
error_t create_idle_thread(Tcb_Base* root_task)
{
        Thread_Base* cpu_idle_thread_ptr = percpu(idle_thread_ptr) =
                new_thread();
        add_thread_to_task(root_task, cpu_idle_thread_ptr);

        return 0;
}