#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
#include <rendezvos/sync/spin_lock.h>
DEFINE_PER_CPU(Task_Manager*, core_tm);
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        struct list_entry* next = current_thread->sched_thread_list.next;
        if(next == &(tm->sched_thread_list)){
                next = next->next;
        }
        return container_of(next,Thread_Base,sched_thread_list);
}
void choose_schedule(Task_Manager* tm){
        tm->schedule=round_robin_schedule;
}