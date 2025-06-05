#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>
extern struct allocator* kallocator;
extern Thread_Base* init_thread_ptr;
extern Thread_Base* idle_thread_ptr;
DEFINE_PER_CPU(Task_Manager*, core_tm);
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        struct list_entry* next = tm->current_thread->sched_thread_list.next;
        if (next == &(tm->sched_thread_list)) {
                next = next->next;
        }
        return container_of(next, Thread_Base, sched_thread_list);
}
Task_Manager* new_task_manager()
{
        struct allocator* cpu_allocator = percpu(kallocator);
        Task_Manager* tm = (Task_Manager*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Task_Manager)));
        choose_schedule(tm);
        INIT_LIST_HEAD(&(tm->sched_task_list));
        INIT_LIST_HEAD(&(tm->sched_thread_list));
        return tm;
}
void choose_schedule(Task_Manager* tm)
{
        tm->schedule = round_robin_schedule;
}
void schedule(Task_Manager* tm)
{
        if (!tm)
                return;
        Thread_Base* curr = tm->current_thread;
        tm->current_thread = tm->schedule(tm);

        if ((tm->current_thread->flags) & THREAD_FLAG_USER) {
                /*
                if target thread is not a kernel thread,
                try to change the vspace
                */
                Tcb_Base* old = curr->belong_tcb;
                Tcb_Base* new = tm->current_thread->belong_tcb;
                if (old != new) {
                        /*
                        we think every task have a vspace
                        */
                        arch_set_current_user_vspace_root(new->vs->vspace_root);
                }
        }

        context_switch(&(curr->ctx), &(tm->current_thread->ctx));
}