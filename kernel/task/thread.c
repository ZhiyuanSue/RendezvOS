#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>

extern struct nexus_node* nexus_root;
/*
we first generate a context that after the return will goto thread entry（this
function) then the stack frame is the only one thread_entry frame then here we
change the return addr, change the parameter then after this return , we will
run the target function
*/
static void thread_entry()
{
        // pr_info("go into the thread_entry\n");
        Thread_Base* current_thread = percpu(core_tm)->current_thread;
        /*get the parameter*/
        if (!current_thread->init_parameter) {
                pr_error("[Error] no any target func is set\n");
                return;
        }
        /*run the target thread*/
        run_thread(current_thread->init_parameter);
        /*finish run the target thread and prepare the clean*/
        pr_info("go back to thread entry and try to clean\n");
        thread_set_status(thread_status_zombie, current_thread);
        schedule(percpu(core_tm));
}
/*general thread create function*/
Thread_Base* create_thread(void* __func, int nr_parameter, ...)
{
        Thread_Base* thread = new_thread();
        va_list arg_list;
        va_start(arg_list, nr_parameter);
        /*
                TODO: we alloc a page as idle thread's stack, we must record
                although idle thread is always exist.
        */
        void* kstack = get_free_page(thread_kstack_page_num,
                                     ZONE_NORMAL,
                                     KERNEL_VIRT_OFFSET,
                                     0,
                                     percpu(nexus_root));
        thread->kstack_bottom =
                (vaddr)kstack + thread_kstack_page_num * PAGE_SIZE;
        memset(kstack, '\0', thread_kstack_page_num * PAGE_SIZE);
        arch_set_new_thread_ctx(&(thread->ctx),
                                (void*)(thread_entry),
                                kstack + thread_kstack_page_num * PAGE_SIZE);
        /*
        set the init parameter of the thread
        the parameter must no more then the NR_ABI_PARAMETER_INT_REG
        and must all are integer, otherwise more parameters will be ignore
        */
        for (int i = 0; i < nr_parameter && i < NR_ABI_PARAMETER_INT_REG; i++) {
                /*here we think in rendezvos kernel,we only use the int
                 * parameters*/
                thread->init_parameter->int_para[i] = va_arg(arg_list, u64);
        }
        thread->init_parameter->thread_func_ptr = __func;
        va_end(arg_list);
        return thread;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread)
{
        error_t res = 0;
        res = add_thread_to_task(task, thread);
        if (res)
                return res;
        res = add_thread_to_manager(percpu(core_tm), thread);
        thread_set_status(thread_status_active_ready, thread);
        return res;
}