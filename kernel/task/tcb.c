#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <common/string.h>
#include <rendezvos/mm/nexus.h>

u64 thread_kstack_page_num = 2;
u64 thread_ustack_page_num = 8;
extern struct allocator* kallocator;
extern struct nexus_node* nexus_root;

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
                /*manually set the status of the thread*/
                thread_set_status(thread_status_active_ready,
                                  percpu(init_thread_ptr));
                thread_set_status(thread_status_running,
                                  percpu(idle_thread_ptr));
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
        if (thread->belong_tcb != NULL) {
                if (thread->belong_tcb->pid == task->pid) {
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
        thread->belong_tcb = task;

        return 0;
}
error_t del_thread_from_task(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread)
                return -E_IN_PARAM;
        /*first check whether the thread belongs to the task*/
        if (thread->belong_tcb != task) {
                pr_error("[ERROR] try to delete a thread from another task\n");
                return -E_RENDEZVOS;
        }
        list_del_init(&(thread->thread_list_node));
        thread->belong_tcb = NULL;
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
        if (!cpu_allocator)
                return NULL;
        Tcb_Base* tcb = (Tcb_Base*)(cpu_allocator->m_alloc(cpu_allocator,
                                                           sizeof(Tcb_Base)));
        if (tcb) {
                memset((void*)tcb, 0, sizeof(Tcb_Base));
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
        if (!cpu_allocator)
                return NULL;
        Thread_Base* thread = (Thread_Base*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Thread_Base)));

        if (thread) {
                memset((void*)thread, 0, sizeof(Thread_Base));
                thread->tid = INVALID_ID;
                arch_task_ctx_init(&(thread->ctx));
                thread_set_status(thread_status_init, thread);
                INIT_LIST_HEAD(&(thread->sched_thread_list));
                INIT_LIST_HEAD(&(thread->thread_list_node));
                thread->belong_tcb = NULL;
                thread->tm = NULL;
                thread->kstack_bottom = 0;
                thread->init_parameter = new_init_parameter();
                thread->flags = THREAD_FLAG_NONE;
        }
        return thread;
}
VSpace* new_vspace()
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return NULL;
        VSpace* new_vs = (VSpace*)(cpu_allocator->m_alloc(cpu_allocator,
                                                          sizeof(VSpace)));
        if (new_vs)
                memset((void*)new_vs, 0, sizeof(VSpace));
        return new_vs;
}
void del_vspace(VSpace** vs)
{
        if (!(*vs))
                return;
        nexus_delete_vspace(
                per_cpu(nexus_root,
                        ((struct nexus_node*)((*vs)->_vspace_node))->nexus_id),
                (*vs)->_vspace_node);

        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return;
        cpu_allocator->m_free(cpu_allocator, (void*)(*vs));
        *vs = NULL;
}
Thread_Init_Para* new_init_parameter()
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return NULL;
        Thread_Init_Para* new_pm = (Thread_Init_Para*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Thread_Init_Para)));
        if (new_pm) {
                new_pm->thread_func_ptr = NULL;
                memset(new_pm->int_para,
                       '\0',
                       (NR_ABI_PARAMETER_INT_REG) * sizeof(u64));
        }
        return new_pm;
}
void del_init_parameter(Thread_Init_Para* pm)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return;
        cpu_allocator->m_free(cpu_allocator, (void*)pm);
}