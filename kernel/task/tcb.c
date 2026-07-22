#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <common/string.h>
#include <common/refcount.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>

u64 thread_kstack_page_num = 2;
u64 thread_ustack_page_num = 8;

DEFINE_PER_CPU(Thread_Base*, init_thread_ptr);
char init_thread_name[] = "init_thread";

DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
char idle_thread_name[] = "idle_thread";

void* idle_thread(void* arg)
{
        (void)arg;
        while (1) {
                /*TODO:might close the int*/
                schedule(percpu(core_tm));
        }
}
error_t create_init_thread(Tcb_Base* root_task)
{
        if (!root_task || !percpu(core_tm))
                return -E_IN_PARAM;
        /*we let the current execution flow as init thread*/
        Thread_Base* init_t = percpu(init_thread_ptr) =
                new_thread_structure(percpu(kallocator), NULL);

        error_t e = -E_RENDEZVOS;
        if (!init_t) {
                pr_error("[ Error ] new thread structure fail\n");
                goto new_thread_fail;
        }
        ref_init(&init_t->refcount);
        init_t->tid = get_new_id(&tid_manager);
        e = add_thread_to_task(root_task, init_t);
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] add thread to task fail\n");
                goto add_thread_to_task_fail;
        }
        e = add_thread_to_manager(percpu(core_tm), init_t);
        if (e != REND_SUCCESS) {
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
        lock_cas(&root_task->thread_list_lock);
        list_del_init(&(init_t->thread_list_node));
        if (root_task->thread_number > 0)
                root_task->thread_number--;
        unlock_cas(&root_task->thread_list_lock);
add_thread_to_task_fail:
        del_thread_structure(init_t);
new_thread_fail:
        return e;
}

static init_thread_ipc_handler_fn init_thread_ipc_handler;

void kernel_set_ipc_handler(init_thread_ipc_handler_fn handler)
{
        init_thread_ipc_handler = handler;
}

error_t kernel_port_register(void)
{
        Message_Port_t* port;
        error_t err;

        if (!global_port_table) {
                return -E_RENDEZVOS;
        }

        port = create_message_port(KERNEL_PORT_NAME);
        if (!port) {
                pr_error("[kernel_port] create_message_port '%s' failed\n",
                         KERNEL_PORT_NAME);
                return -E_RENDEZVOS;
        }

        err = register_port(global_port_table, port);
        if (err != REND_SUCCESS) {
                pr_error("[kernel_port] register_port '%s' failed e=%d\n",
                         KERNEL_PORT_NAME,
                         (int)err);
                delete_message_port_structure(port);
                return err;
        }

        pr_info("[kernel_port] registered '%s' service_id=%u\n",
                KERNEL_PORT_NAME,
                (unsigned)port->service_id);
        ref_put(&port->refcount, free_message_port_ref);
        return REND_SUCCESS;
}

error_t kernel_handle_msg(void)
{
        Message_Port_t* port;

        port = thread_lookup_port(KERNEL_PORT_NAME);
        if (!port) {
                pr_error("[init_thread] lookup '%s' failed\n",
                         KERNEL_PORT_NAME);
                return -E_RENDEZVOS;
        }

        for (;;) {
                error_t e = recv_msg(port);

                if (e != REND_SUCCESS) {
                        pr_error("[init_thread] recv_msg failed e=%d\n",
                                 (int)e);
                        continue;
                }

                Message_t* msg;

                while ((msg = dequeue_recv_msg()) != NULL) {
                        u16 service_id = port->service_id;

                        if (init_thread_ipc_handler) {
                                init_thread_ipc_handler(msg, service_id);
                        } else {
                                ref_put(&msg->ms_queue_node.refcount,
                                        free_message_ref);
                        }
                }
        }
}

error_t create_idle_thread(void)
{
        error_t e = gen_thread_from_func(&percpu(idle_thread_ptr),
                                         idle_thread,
                                         idle_thread_name,
                                         percpu(core_tm),
                                         NULL);
        if (e != REND_SUCCESS) {
                pr_error("[ Error ]idle thread init fail\n");
                return e;
        }
        return REND_SUCCESS;
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
        percpu(core_tm)->root_task =
                new_task_structure(percpu(kallocator), NULL);
        if (!percpu(core_tm)->root_task) {
                pr_error("[ Error ] new root task fail\n");
                goto new_root_task_fail;
        }
        percpu(core_tm)->root_task->pid = get_new_id(&pid_manager);
        percpu(core_tm)->root_task->vs = percpu(current_vspace);
        e = add_task_to_manager(percpu(core_tm), percpu(core_tm)->root_task);
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] add root task to manager fail\n");
                goto add_task_to_manager_fail;
        }

        e = create_init_thread(percpu(core_tm)->root_task);
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] create init thread fail %d\n", e);
                goto create_init_thread_fail;
        }
        e = create_idle_thread();
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] create idle thread fail %d\n", e);
                goto create_idle_thread_fail;
        }
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
create_idle_thread_fail:
        lock_cas(&percpu(core_tm)->root_task->thread_list_lock);
        list_del_init(&(percpu(init_thread_ptr)->thread_list_node));
        if (percpu(core_tm)->root_task->thread_number > 0)
                percpu(core_tm)->root_task->thread_number--;
        unlock_cas(&percpu(core_tm)->root_task->thread_list_lock);
        del_thread_structure(percpu(init_thread_ptr));
create_init_thread_fail:
        if (del_task_from_manager(percpu(core_tm)->root_task) != REND_SUCCESS) {
                pr_error(
                        "fail to delete task from task manager, please check\n");
        }
add_task_to_manager_fail:
        delete_task(percpu(core_tm)->root_task);
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
        lock_cas(&task->thread_list_lock);
        list_add_tail(&(thread->thread_list_node), &(task->thread_head_node));
        task->thread_number++;
        thread->belong_tcb = task;
        unlock_cas(&task->thread_list_lock);
        return REND_SUCCESS;
}
error_t del_thread_from_task(Thread_Base* thread)
{
        if (!thread)
                return -E_IN_PARAM;
        Tcb_Base* task = thread->belong_tcb;
        if (task) {
                lock_cas(&task->thread_list_lock);
                list_del_init(&(thread->thread_list_node));
                if (task->thread_number > 0)
                        task->thread_number--;
                thread->belong_tcb = NULL;
                unlock_cas(&task->thread_list_lock);
                return REND_SUCCESS;
        }
        /*
         * Idempotent: already detached from task. If pointers were cleared
         * without list_del_init (bug), unlink last-chance before Thread_Base
         * free (e.g. del_thread_structure).
         */
        if (!list_node_is_detached(&thread->thread_list_node)) {
                pr_error(
                        "[thread] del_thread_from_task: orphan thread_list_node\n");
                list_del_init(&thread->thread_list_node);
        }
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
        lock_cas(&core_tm->sched_lock);
        list_add_tail(&(task->sched_task_list), &(core_tm->sched_task_list));
        unlock_cas(&core_tm->sched_lock);
        task->tm = core_tm;
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
        lock_cas(&task->tm->sched_lock);
        list_del_init(&task->sched_task_list);
        unlock_cas(&task->tm->sched_lock);
        task->tm = NULL;
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
        lock_cas(&core_tm->sched_lock);
        list_add_tail(&(thread->sched_thread_list),
                      &(core_tm->sched_thread_list));
        thread->tm = core_tm;
        unlock_cas(&core_tm->sched_lock);

        bool is_init_status = thread_set_status_with_expect(
                thread, thread_status_init, thread_status_ready);
        if (!is_init_status) {
                pr_warn("[ERROR] a thread add to the manager with a status not init\n");
        }
        return REND_SUCCESS;
}
error_t del_thread_from_manager(Thread_Base* thread)
{
        if (!thread)
                return -E_IN_PARAM;
        if (thread->tm) {
                Task_Manager* core_tm = thread->tm;
                lock_cas(&core_tm->sched_lock);
                /* Still running on owner CPU: caller must schedule away first.
                 */
                if (core_tm->current_thread == thread) {
                        unlock_cas(&core_tm->sched_lock);
                        return -E_REND_AGAIN;
                }
                list_del_init(&thread->sched_thread_list);
                unlock_cas(&core_tm->sched_lock);
                thread->tm = NULL;
                return REND_SUCCESS;
        }
        /*
         * Idempotent: already detached from Task_Manager. Defensive unlink if
         * sched_thread_list is still embedded (tm cleared without list_del).
         */
        if (!list_node_is_detached(&thread->sched_thread_list)) {
                pr_error(
                        "[thread] del_thread_from_manager: orphan sched_thread_list\n");
                list_del_init(&thread->sched_thread_list);
        }
        return REND_SUCCESS;
}

Tcb_Base* new_task_structure(struct allocator* cpu_kallocator,
                             const task_append_hooks_t* append_hooks)
{
        size_t append_tcb_info_len =
                append_hooks ? append_hooks->append_info_len : 0;

        if (!cpu_kallocator)
                return NULL;
        Tcb_Base* tcb = (Tcb_Base*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Tcb_Base) + append_tcb_info_len));
        if (tcb) {
                tcb->pid = INVALID_ID;
                tcb->append_hooks = append_hooks;
                lock_init_cas(&tcb->thread_list_lock);
                INIT_LIST_HEAD(&(tcb->sched_task_list));
                tcb->thread_number = 0;
                INIT_LIST_HEAD(&(tcb->thread_head_node));
                tcb->vs = NULL;
                tcb->tm = NULL;
                if (append_hooks && append_hooks->init) {
                        if (append_hooks->init((struct Tcb_Base*)tcb)
                            != REND_SUCCESS) {
                                cpu_kallocator->m_free(cpu_kallocator, tcb);
                                return NULL;
                        }
                }
        }
        return tcb;
}
error_t delete_task(Tcb_Base* tcb)
{
        if (!tcb)
                return -E_IN_PARAM;
        lock_cas(&tcb->thread_list_lock);
        bool has_threads = (tcb->thread_number != 0);
        unlock_cas(&tcb->thread_list_lock);
        if (has_threads)
                return -E_RENDEZVOS;

        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator)
                return -E_RENDEZVOS;

        error_t e = REND_SUCCESS;

        /* First detach from manager so we don't risk freeing a node still
         * linked. */
        e = del_task_from_manager(tcb);
        if (e != REND_SUCCESS)
                return e;

        if (tcb->vs) {
                VSpace* vspace = tcb->vs;
                tcb->vs = NULL;
                if (vspace != &root_vspace) {
                        /*
                         * User address spaces: radix metadata + PTE teardown
                         * run in del_vspace() when the last refcount drops
                         * (free_vspace_ref). Do not duplicate unmap/radix tree
                         * work here.
                         */
                        error_t put_e =
                                ref_put(&vspace->refcount, free_vspace_ref);
                        if (put_e != REND_SUCCESS) {
                                e = put_e;
                                pr_error(
                                        "[task] user vspace ref_put vs=%p e=%d\n",
                                        (void*)vspace,
                                        (int)e);
                        }
                }
        }

        if (tcb->append_hooks && tcb->append_hooks->fini)
                tcb->append_hooks->fini((struct Tcb_Base*)tcb);

        cpu_kallocator->m_free(cpu_kallocator, tcb);
        return e;
}