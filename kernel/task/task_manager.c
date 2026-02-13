#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>
#include <modules/log/log.h>
#include <rendezvos/mm/allocator.h>
extern Thread_Base* init_thread_ptr;
extern Thread_Base* idle_thread_ptr;
DEFINE_PER_CPU(Task_Manager*, core_tm);
volatile bool is_print_sche_info;
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        struct list_entry* next = tm->current_thread->sched_thread_list.next;
        while (next == &(tm->sched_thread_list)
               || container_of(next, Thread_Base, sched_thread_list)->status
                          != thread_status_ready) {
                next = next->next;
        }
        return container_of(next, Thread_Base, sched_thread_list);
}
Task_Manager* new_task_manager(void)
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
        tm->scheduler = round_robin_schedule;
        is_print_sche_info = true;
}
void print_sche_info(Thread_Base* old, Thread_Base* new)
{
        if (!is_print_sche_info)
                return;
        if (old->name) {
                if (new->name) {
                        pr_info("[CPU %d SCHE INFO] old %s new %s\n",
                                percpu(cpu_number),
                                old->name,
                                new->name);
                } else {
                        pr_info("[CPU %d SCHED INFO] old %s new %x\n",
                                percpu(cpu_number),
                                old->name,
                                new);
                }

        } else {
                if (new->name) {
                        pr_info("[CPU %d SCHED INFO] old %x new %s\n",
                                percpu(cpu_number),
                                old,
                                new->name);
                } else {
                        pr_info("[CPU %d SCHED INFO] old %x new %x\n",
                                percpu(cpu_number),
                                old,
                                new);
                }
        }
}
void schedule(Task_Manager* tm)
{
        if (!tm)
                return;
        Thread_Base* curr = tm->current_thread;
        tm->current_thread = tm->scheduler(tm);
        if (curr == tm->current_thread) /*still running current thread*/
                return;
        print_sche_info(curr, tm->current_thread);

        if ((tm->current_thread->flags) & THREAD_FLAG_USER) {
                /*
                if target thread is not a kernel thread,
                try to change the vspace
                */
                Tcb_Base* old = curr->belong_tcb;
                Tcb_Base* new = tm->current_thread->belong_tcb;
                if (old != new) {
                        /*
                         * we think every task have a vspace
                         */
                        tm->current_task = new;
                        arch_set_current_user_vspace_root(
                                new->vs->vspace_root_addr);
                }
        }
        /*
         * if before the schedule no status is set
         * set it to ready, otherwise using the set status
         */
        thread_set_status_with_expect(
                curr, thread_status_running, thread_status_ready);
        thread_set_status(tm->current_thread, thread_status_running);
        switch_to(&(curr->ctx), &(tm->current_thread->ctx));
}