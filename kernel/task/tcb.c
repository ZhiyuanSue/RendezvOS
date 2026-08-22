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

DEFINE_PER_CPU(Thread_Base*, boot_thread_ptr);
char boot_thread_name[] = "boot_thread";

DEFINE_PER_CPU(Thread_Base*, idle_thread_ptr);
char idle_thread_name[] = "idle_thread";

void* idle_thread(void* arg)
{
        (void)arg;
        while (1) {
                schedule(percpu(core_tm));
        }
}
error_t create_boot_thread(void)
{
        if (!percpu(core_tm))
                return -E_IN_PARAM;
        /*we let the current execution flow as boot thread*/
        Thread_Base* boot_t = percpu(boot_thread_ptr) =
                new_thread_structure(percpu(kallocator), NULL);

        error_t e = -E_RENDEZVOS;
        if (!boot_t) {
                pr_error("[ Error ] new thread structure fail\n");
                goto new_thread_fail;
        }
        ref_init(&boot_t->refcount);
        boot_t->tid = get_new_id(&tid_manager);
        e = add_thread_to_manager(percpu(core_tm), boot_t);
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] add thread to manager fail\n");
                goto add_thread_to_manager_fail;
        }
        /*we have to set the kstack bottom to the percpu stack*/
        boot_t->kstack_bottom = percpu(boot_stack_bottom);
        thread_set_status(boot_t, thread_status_running); /*boot thread is the
                                                             running thread*/
        thread_set_name(boot_thread_name, boot_t);
        return REND_SUCCESS;
add_thread_to_manager_fail:
        del_thread_structure(boot_t);
new_thread_fail:
        return e;
}

static boot_thread_ipc_handler_fn boot_thread_ipc_handler;

void kernel_set_ipc_handler(boot_thread_ipc_handler_fn handler)
{
        boot_thread_ipc_handler = handler;
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
                pr_error("[boot_thread] lookup '%s' failed\n",
                         KERNEL_PORT_NAME);
                return -E_RENDEZVOS;
        }

        for (;;) {
                error_t e = recv_msg(port);

                if (e != REND_SUCCESS) {
                        pr_error("[boot_thread] recv_msg failed e=%d\n",
                                 (int)e);
                        continue;
                }

                Message_t* msg;

                while ((msg = dequeue_recv_msg()) != NULL) {
                        u16 service_id = port->service_id;

                        if (boot_thread_ipc_handler) {
                                boot_thread_ipc_handler(msg, service_id);
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
        e = create_boot_thread();
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] create boot thread fail %d\n", e);
                goto create_boot_thread_fail;
        }
        e = create_idle_thread();
        if (e != REND_SUCCESS) {
                pr_error("[ Error ] create idle thread fail %d\n", e);
                goto create_idle_thread_fail;
        }
        if (percpu(boot_thread_ptr) && percpu(idle_thread_ptr)) {
                percpu(core_tm)->current_thread = percpu(idle_thread_ptr);
                /*manually set the status of the thread*/
                thread_set_status(percpu(boot_thread_ptr), thread_status_ready);
                thread_set_status(percpu(idle_thread_ptr),
                                  thread_status_running);
                switch_to(&(percpu(boot_thread_ptr)->ctx),
                          &(percpu(idle_thread_ptr)->ctx));
        } else {
                pr_error("[Error] boot_proc fail\n");
                return NULL;
        }
        return percpu(core_tm);
create_idle_thread_fail:
        (void)del_thread_from_manager(percpu(boot_thread_ptr));
        del_thread_structure(percpu(boot_thread_ptr));
create_boot_thread_fail:
        del_task_manager_structure(percpu(core_tm));
new_task_manager_fail:
        return NULL;
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