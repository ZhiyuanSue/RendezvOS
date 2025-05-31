#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
#include <rendezvos/sync/spin_lock.h>
extern struct allocator* kallocator;
DEFINE_PER_CPU(Task_Manager*, core_tm);
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        struct list_entry* next = current_thread->sched_thread_list.next;
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
        Thread_Base* next = tm->schedule(tm);
        Thread_Base* curr = percpu(current_thread);
        percpu(current_thread) = next;
        context_switch(&(curr->ctx), &(next->ctx));
}