#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <common/string.h>
#include <rendezvos/mm/allocator.h>

u64 thread_kstack_page_num = 2;
u64 thread_ustack_page_num = 8;

Task_Manager* init_proc(void)
{
        percpu(core_tm) = new_task_manager();
        /*create the root task and init it*/
        Tcb_Base* root_task = new_task_structure();
        root_task->pid = get_new_pid();
        root_task->vs = percpu(current_vspace);
        add_task_to_manager(percpu(core_tm), root_task);
        percpu(core_tm)->current_task = root_task;

        create_init_thread(root_task);
        create_idle_thread(root_task);
        if (percpu(init_thread_ptr) && percpu(idle_thread_ptr)) {
                percpu(core_tm)->current_thread = percpu(idle_thread_ptr);
                /*manually set the status of the thread*/
                thread_set_status(thread_status_ready, percpu(init_thread_ptr));
                thread_set_status(thread_status_running,
                                  percpu(idle_thread_ptr));
                switch_to(&(percpu(init_thread_ptr)->ctx),
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
        task->thread_number++;
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
        thread->belong_tcb->thread_number--;
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
error_t del_task_from_manager(Tcb_Base* task)
{
        if (!task)
                return E_IN_PARAM;
        if (!task->tm) {
                pr_error("[ERROR] this task not belong to any manager\n");
                return -E_RENDEZVOS;
        }
        list_del_init(&task->sched_task_list);
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
error_t del_thread_from_manager(Thread_Base* thread)
{
        if (!thread)
                return E_IN_PARAM;
        if (!thread->tm) {
                pr_error("[ERROR] this task not belong to any manager\n");
                return -E_RENDEZVOS;
        }
        list_del_init(&thread->sched_thread_list);
        return 0;
}

Tcb_Base* new_task_structure(void)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return NULL;
        Tcb_Base* tcb = (Tcb_Base*)(cpu_allocator->m_alloc(cpu_allocator,
                                                           sizeof(Tcb_Base)));
        Message_t* dummy_recv_msg_node = (Message_t*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Message_t)));
        Message_t* dummy_send_msg_node = (Message_t*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Message_t)));
        if (tcb) {
                memset((void*)tcb, 0, sizeof(Tcb_Base));
                tcb->pid = INVALID_ID;
                INIT_LIST_HEAD(&(tcb->sched_task_list));
                tcb->thread_number = 0;
                INIT_LIST_HEAD(&(tcb->thread_head_node));
                tcb->vs = NULL;
                tcb->tm = NULL;
                if (dummy_recv_msg_node)
                        msq_init(&tcb->recv_msg_queue, dummy_recv_msg_node, 0);
                if (dummy_send_msg_node)
                        msq_init(&tcb->send_msg_queue, dummy_send_msg_node, 0);
        }
        return tcb;
}
void delete_task(Tcb_Base* tcb)
{
        if (!tcb)
                return;
        if (tcb->thread_number)
                return;

        del_vspace(&tcb->vs);
        del_task_from_manager(tcb);

        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return;
        cpu_allocator->m_free(cpu_allocator, tcb);
        return;
}