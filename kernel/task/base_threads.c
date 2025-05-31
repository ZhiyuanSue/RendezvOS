#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(Thread_Base*, init_thread_ptr);
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
/* This is the idle thread function*/
void idle_thread()
{
        /*TODO:might close the int*/
        schedule(percpu(core_tm));
}
error_t create_init_thread(Tcb_Base* root_task)
{
        /*we let the current execution flow as init thread*/
        Thread_Base* init_ptr = percpu(init_thread_ptr) = new_thread();
        add_thread_to_task(root_task, init_ptr);
        add_thread_to_manager(percpu(core_tm), init_ptr);

        return 0;
}
error_t create_idle_thread(Tcb_Base* root_task)
{
        Thread_Base* idle_ptr = percpu(idle_thread_ptr) = new_thread();
        add_thread_to_task(root_task, idle_ptr);
        add_thread_to_manager(percpu(core_tm), idle_ptr);
        /*
                TODO: we alloc a page as idle thread's stack, we must record
                although idle thread is always exist.
        */
        void* start_ptr = NULL;
        arch_set_idle_thread_ctx(
                &(idle_ptr->ctx), (void*)(idle_thread), start_ptr);

        return 0;
}