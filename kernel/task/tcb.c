#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>

u64 thread_kstack_page_num = 2;
extern struct allocator* kallocator;

Task_Manager* init_proc()
{
        percpu(core_tm) = new_task_manager();
        /*create the root task and init it*/
        Tcb_Base* root_task = new_task();
        root_task->pid = get_new_pid();
        root_task->vs = percpu(current_vspace);
        add_task_to_manager(percpu(core_tm), root_task);
        percpu(core_tm)->current_task = root_task;

        create_init_thread(root_task);
        create_idle_thread(root_task);
        if (percpu(init_thread_ptr) && percpu(idle_thread_ptr)) {
                percpu(core_tm)->current_thread = idle_thread_ptr;
                context_switch(&(percpu(init_thread_ptr)->ctx),
                               &(percpu(idle_thread_ptr)->ctx));
        } else {
                pr_error("[Error] init_proc fail\n");
        }
        return percpu(core_tm);
}
error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread)
                return -E_IN_PARAM;
        /*first do some checks*/
        if (thread->belong_pid != INVALID_ID) {
                if (thread->belong_pid == task->pid) {
                        pr_debug(
                                "[ERROR] try to readd the thread to same task\n");
                } else {
                        pr_error(
                                "[ERROR] try to readd the thread to another task\n");
                        return -E_RENDEZVOS;
                }
        }
        /*we do not check the linked list */
        list_add_tail(&(thread->thread_list_node), &(task->thread_head_node));
        thread->belong_pid = task->pid;

        return 0;
}
error_t del_thread_from_task(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread)
                return -E_IN_PARAM;
        /*first check whether the thread belongs to the task*/
        if (thread->belong_pid != task->pid) {
                pr_error("[ERROR] try to delete a thread from another task\n");
                return -E_RENDEZVOS;
        }
        list_del_init(&(thread->thread_list_node));
        thread->belong_pid = INVALID_ID;
        return 0;
}
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task)
{
        if (!core_tm || !task)
                return E_IN_PARAM;
        if (task->tm) {
                pr_error("[ERROR] this task have has a manager\n");
                return -E_RENDEZVOS;
        }
        list_add_tail(&(task->sched_task_list), &(core_tm->sched_task_list));
        return 0;
}
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread)
{
        if (!core_tm || !thread)
                return 0;
        if (thread->tm) {
                pr_error("[ERROR] this thread have has a manager\n");
                return -E_RENDEZVOS;
        }
        list_add_tail(&(thread->sched_thread_list),
                      &(core_tm->sched_thread_list));
        return 0;
}

Tcb_Base* new_task()
{
        struct allocator* cpu_allocator = percpu(kallocator);
        Tcb_Base* tcb = (Tcb_Base*)(cpu_allocator->m_alloc(cpu_allocator,
                                                           sizeof(Tcb_Base)));
        if (tcb) {
                tcb->pid = INVALID_ID;
                INIT_LIST_HEAD(&(tcb->sched_task_list));
                INIT_LIST_HEAD(&(tcb->thread_head_node));
                tcb->vs = NULL;
                tcb->tm = NULL;
        }
        return tcb;
}
Thread_Base* new_thread()
{
        struct allocator* cpu_allocator = percpu(kallocator);
        Thread_Base* thread = (Thread_Base*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Thread_Base)));
        if (thread) {
                thread->tid = INVALID_ID;
                arch_task_ctx_init(&(thread->ctx));
                thread->status = tcb_status_init;
                INIT_LIST_HEAD(&(thread->sched_thread_list));
                INIT_LIST_HEAD(&(thread->thread_list_node));
                thread->belong_pid = INVALID_ID;
                thread->tm = NULL;
                thread->kstack_bottom = 0;
        }
        return thread;
}