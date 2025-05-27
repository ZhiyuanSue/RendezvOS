#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
#include <rendezvos/mm/spmalloc.h>
extern struct allocator* kallocator;
DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
error_t create_idle_thread(Tcb_Base* root_task)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        Thread_Base* cpu_idle_thread_ptr = percpu(idle_thread_ptr) =
                (Thread_Base*)(cpu_allocator->m_alloc(cpu_allocator,
                                                      sizeof(Thread_Base)));
        list_add_head(&(cpu_idle_thread_ptr->thread_list_node),
                      &(root_task->thread_head_node));

        return 0;
}