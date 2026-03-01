#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <common/string.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/task/initcall.h>

u64 thread_kstack_page_num = 2;
u64 thread_ustack_page_num = 8;

DEFINE_PER_CPU(Thread_Base*, init_thread_ptr);
char init_thread_name[] = "init_thread";

error_t create_init_thread(Tcb_Base* root_task)
{
        if (!root_task || !percpu(core_tm))
                return -E_IN_PARAM;
        /*we let the current execution flow as init thread*/
        Thread_Base* init_t = percpu(init_thread_ptr) =
                new_thread_structure(percpu(kallocator), 0);

        error_t e = -E_RENDEZVOS;
        if (!init_t) {
                pr_error("[ Error ] new thread structure fail\n");
                goto new_thread_fail;
        }
        init_t->tid = get_new_tid();
        e = add_thread_to_task(root_task, init_t);
        if (e) {
                pr_error("[ Error ] add thread to task fail\n");
                goto add_thread_to_task_fail;
        }
        e = add_thread_to_manager(percpu(core_tm), init_t);
        if (e) {
                pr_error("[ Error ] add thread to manager fail\n");
                goto add_thread_to_manager_fail;
        }
        /*we have to set the kstack bottom to the percpu stack*/
        init_t->kstack_bottom = percpu(boot_stack_bottom);
        thread_set_status(init_t, thread_status_running); /*init thread is the
                                                             running thread*/
        thread_set_name(init_thread_name, init_t);
        return REND_SUCCESS;
add_thread_to_manager_fail:
        list_del_init(&(init_t->thread_list_node));
        root_task->thread_number--;
add_thread_to_task_fail:
        del_thread_structure(init_t);
new_thread_fail:
        return e;
}
Task_Manager* init_proc(void)
{
        error_t e = -E_RENDEZVOS;
        percpu(core_tm) = new_task_manager();
        if (!percpu(core_tm)) {
                pr_error("[ Error ] new task manager fail\n");
                goto new_task_manager_fail;
        }
        /*create the root task and init it*/
        Tcb_Base* root_task = new_task_structure(percpu(kallocator), 0);
        if (!root_task) {
                pr_error("[ Error ] new root task fail\n");
                goto new_root_task_fail;
        }
        root_task->pid = get_new_pid();
        root_task->vs = percpu(current_vspace);
        e = add_task_to_manager(percpu(core_tm), root_task);
        if (e) {
                pr_error("[ Error ] add root task to manager fail\n");
                goto add_task_to_manager_fail;
        }
        percpu(core_tm)->current_task = root_task;

        e = create_init_thread(root_task);
        if (e) {
                pr_error("[ Error ] create init thread fail %d\n", e);
                goto create_init_thread_fail;
        }
        do_init_call();
        if (percpu(init_thread_ptr) && percpu(idle_thread_ptr)) {
                percpu(core_tm)->current_thread = percpu(idle_thread_ptr);
                /*manually set the status of the thread*/
                thread_set_status(percpu(init_thread_ptr), thread_status_ready);
                thread_set_status(percpu(idle_thread_ptr),
                                  thread_status_running);
                switch_to(&(percpu(init_thread_ptr)->ctx),
                          &(percpu(idle_thread_ptr)->ctx));
        } else {
                pr_error("[Error] init_proc fail\n");
                return NULL;
        }
        return percpu(core_tm);
create_init_thread_fail:
        if (del_task_from_manager(root_task) != REND_SUCCESS) {
                pr_error(
                        "fail to delete task from task manager, please check\n");
        }
add_task_to_manager_fail:
        delete_task(root_task);
new_root_task_fail:
        del_task_manager_structure(percpu(core_tm));
new_task_manager_fail:
        return NULL;
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

        return REND_SUCCESS;
}
error_t del_thread_from_task(Thread_Base* thread)
{
        if (!thread)
                return -E_IN_PARAM;
        if (!thread->belong_tcb)
                return REND_SUCCESS;
        thread->belong_tcb->thread_number--;
        thread->belong_tcb = NULL;
        list_del_init(&(thread->thread_list_node));
        return REND_SUCCESS;
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
        return REND_SUCCESS;
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
        return REND_SUCCESS;
}
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread)
{
        if (!core_tm || !thread)
                return REND_SUCCESS;
        if (thread->tm) {
                pr_error("[ERROR] this thread have has a manager\n");
                return -E_RENDEZVOS;
        }
        list_add_tail(&(thread->sched_thread_list),
                      &(core_tm->sched_thread_list));
        return REND_SUCCESS;
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
        return REND_SUCCESS;
}

Tcb_Base* new_task_structure(struct allocator* cpu_allocator,
                             size_t append_tcb_info_len)
{
        if (!cpu_allocator)
                return NULL;
        Tcb_Base* tcb = (Tcb_Base*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Tcb_Base) + append_tcb_info_len));
        if (tcb) {
                memset((void*)tcb, 0, sizeof(Tcb_Base) + append_tcb_info_len);
                tcb->pid = INVALID_ID;
                INIT_LIST_HEAD(&(tcb->sched_task_list));
                tcb->thread_number = 0;
                INIT_LIST_HEAD(&(tcb->thread_head_node));
                tcb->vs = NULL;
                tcb->tm = NULL;
        }
        return tcb;
}
void delete_task(Tcb_Base* tcb)
{
        if (!tcb)
                return;
        if (tcb->thread_number)
                return;

        if (tcb->vs)
                del_vspace(&tcb->vs);
        error_t e = del_task_from_manager(tcb);
        if (e) {
                pr_error(
                        "[ Error ] delete task from manager fail, please check\n");
        }

        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return;
        cpu_allocator->m_free(cpu_allocator, tcb);
        return;
}