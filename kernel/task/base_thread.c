#include <rendezvos/task/tcb.h>
#include <rendezvos/error.h>
#include <modules/log/log.h>
DEFINE_PER_CPU(Thread_Base*, init_thread_ptr);
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
/* This is the idle thread function*/
void idle_thread()
{
        while (1) {
                /*TODO:might close the int*/
                schedule(percpu(core_tm));
        }
}
error_t create_init_thread(Tcb_Base* root_task)
{
        /*we let the current execution flow as init thread*/
        Thread_Base* init_t = percpu(init_thread_ptr) = new_thread();
        add_thread_to_task(root_task, init_t);
        add_thread_to_manager(percpu(core_tm), init_t);
        /*we have to set the kstack bottom to the percpu stack*/
        init_t->kstack_bottom = percpu(boot_stack_bottom);
        thread_set_status(thread_status_running, init_t); /*init thread is the
                                                             running thread*/
        return 0;
}
error_t create_idle_thread(Tcb_Base* root_task)
{
        Thread_Base* idle_t = percpu(idle_thread_ptr) =
                create_thread((void*)idle_thread, 0);
        if (!idle_t) {
                pr_error("[Error] create idle thread fail\n");
                return -E_RENDEZVOS;
        }
        error_t e = thread_join(root_task, idle_t);
        return e;
}