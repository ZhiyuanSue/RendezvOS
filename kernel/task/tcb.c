#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
#include <rendezvos/mm/spmalloc.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
extern struct allocator* kallocator;
DEFINE_PER_CPU(Tcb_Base*, current_task);

Tcb_Base* init_proc()
{
        return NULL;
}
error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread)
                return 0;
        /*first do some checks*/
        if (thread->belong_pid != INVALID_ID) {
                if (thread->belong_pid == task->pid) {
                        pr_debug(
                                "[ERROR] try to readd the thread to same task\n");
                } else {
                        pr_error(
                                "[ERROR] try to readd the thread to another task\n");
                        return -EPERM;
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
                return 0;
        /*first check whether the thread belongs to the task*/
        if (thread->belong_pid != task->pid) {
                pr_error("[ERROR] try to delete a thread from another task\n");
                return -EPERM;
        }
        list_del_init(&(thread->thread_list_node));
        thread->belong_pid = INVALID_ID;
        return 0;
}
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task)
{
        if (!core_tm || !task)
                return 0;
        if (task->tm) {
                pr_error("[ERROR] this task have has a manager\n");
                return -EPERM;
        }
        list_add_tail(&(task->sched_task_list), &(core_tm->sched_task_list));
        return 0;
}
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread)
{
        if (!core_tm || !thread)
                return 0;
        if (thread->tm) {
                pr_error("[ERROR] this thread have hase a manager\n");
                return -EPERM;
        }
        list_add_tail(&(thread->sched_thread_list),
                      &(core_tm->sched_task_list));
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
        }
        return thread;
}