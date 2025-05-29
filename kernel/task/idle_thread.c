#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
/* This is the idle thread function*/
void idle_thread()
{
}
void set_idle_thread(Arch_Task_Context* ctx)
{
}
error_t create_idle_thread(Tcb_Base* root_task)
{
        Thread_Base* idle_ptr = percpu(idle_thread_ptr) = new_thread();
        add_thread_to_task(root_task, idle_ptr);
        add_thread_to_manager(percpu(core_tm), idle_ptr);
        set_idle_thread(&(idle_ptr->ctx));

        return 0;
}