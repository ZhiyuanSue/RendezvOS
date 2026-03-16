#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/task/port.h>
#include <rendezvos/sync/spin_lock.h>
/*
we first generate a context that after the return will goto thread entry（this
function) then the stack frame is the only one thread_entry frame then here we
change the return addr, change the parameter then after this return , we will
run the target function
*/
static void thread_entry(void)
{
        // pr_info("go into the thread_entry\n");
        Thread_Base* current_thread = percpu(core_tm)->current_thread;
        /*get the parameter*/
        if (!current_thread->init_parameter) {
                pr_error("[Error] no any target func is set\n");
                return;
        }
        /*run the target thread*/
        run_thread(current_thread->init_parameter);
        /*
                        finish run the target thread and prepare the clean
                        But the following code should be unreachable for user
           thread (but not kernel thread) in run thread func, when it drop to
           the user address, the sp should set to the kernel stack bottom , so
           the trace of the thread_entry and run_thread will be cover
                        */
        pr_info("go back to thread entry and try to clean\n");
        thread_set_status(current_thread, thread_status_zombie);
        schedule(percpu(core_tm));
}

static void thread_port_cache_init(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        cache->count = 0;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                cache->entries[i].port = NULL;
                cache->entries[i].lru_counter = 0;
        }
}

static void thread_port_cache_clear(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                Message_Port_t* port = cache->entries[i].port;
                if (port)
                        ref_put(&port->refcount, free_message_port_ref);
                cache->entries[i].port = NULL;
                cache->entries[i].lru_counter = 0;
        }
        cache->count = 0;
}

Thread_Base* new_thread_structure(struct allocator* cpu_allocator,
                                  size_t append_thread_info_len)
{
        if (!cpu_allocator)
                return NULL;
        Thread_Base* thread = (Thread_Base*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Thread_Base) + append_thread_info_len));
        if (!thread) {
                goto alloc_thread_error;
        }
        memset((void*)thread, 0, sizeof(Thread_Base) + append_thread_info_len);

        /*first do alloc*/
        thread->init_parameter = new_init_parameter_structure();
        if (!thread->init_parameter) {
                goto alloc_init_param_error;
        }

        Message_t* dummy_recv_msg_node = (Message_t*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Message_t)));
        if (!dummy_recv_msg_node) {
                goto alloc_dummy_recv_msg_error;
        }
        Message_t* dummy_send_msg_node = (Message_t*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Message_t)));
        if (!dummy_send_msg_node) {
                goto alloc_dummy_send_msg_error;
        }

        thread->tid = INVALID_ID;
        arch_task_ctx_init(&(thread->ctx));
        thread_set_status(thread, thread_status_init);
        INIT_LIST_HEAD(&(thread->sched_thread_list));
        INIT_LIST_HEAD(&(thread->thread_list_node));
        thread->belong_tcb = NULL;
        thread->tm = NULL;
        thread->kstack_bottom = 0;
        thread->kstack_num = thread_kstack_page_num;
        thread->name = NULL;
        thread->flags = THREAD_FLAG_NONE;

        /*ipc part*/
        memset(dummy_recv_msg_node, 0, sizeof(Message_t));
        msq_init(&thread->recv_msg_queue,
                 &dummy_recv_msg_node->ms_queue_node,
                 0);

        memset(dummy_send_msg_node, 0, sizeof(Message_t));
        msq_init(&thread->send_msg_queue,
                 &dummy_send_msg_node->ms_queue_node,
                 0);

        thread->send_pending_msg = NULL;
        thread->recv_pending_cnt.counter = 0;
        thread_port_cache_init(&thread->port_cache);
        return thread;

alloc_dummy_send_msg_error:
        cpu_allocator->m_free(cpu_allocator, dummy_recv_msg_node);
alloc_dummy_recv_msg_error:
        cpu_allocator->m_free(cpu_allocator, thread->init_parameter);
alloc_init_param_error:
        cpu_allocator->m_free(cpu_allocator, thread);
alloc_thread_error:
        return NULL;
}
void del_thread_structure(Thread_Base* thread)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!thread || !cpu_allocator)
                return;
        thread_port_cache_clear(&thread->port_cache);
        del_init_parameter_structure(thread->init_parameter);
        cpu_allocator->m_free(cpu_allocator, thread);
}
void free_thread_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Thread_Base* thread =
                container_of(ref_count_ptr, Thread_Base, refcount);
        del_thread_structure(thread);
}

Thread_Init_Para* new_init_parameter_structure(void)
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
void del_init_parameter_structure(Thread_Init_Para* pm)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!pm || !cpu_allocator)
                return;
        cpu_allocator->m_free(cpu_allocator, (void*)pm);
}
/*general thread create function*/
Thread_Base* create_thread(void* __func, size_t append_thread_info_len,
                           int nr_parameter, ...)
{
        Thread_Base* thread = new_thread_structure(percpu(kallocator),
                                                   append_thread_info_len);
        if (!thread) {
                goto new_thread_structure_error;
        }
        ref_init(&thread->refcount);
        thread->tid = get_new_id(&tid_manager);
        va_list arg_list;
        va_start(arg_list, nr_parameter);
        /*
                TODO: we alloc a page as idle thread's stack, we must record
                although idle thread is always exist.
        */
        void* kstack = get_free_page(thread_kstack_page_num,
                                     KERNEL_VIRT_OFFSET,
                                     percpu(nexus_root),
                                     0,
                                     PAGE_ENTRY_NONE);
        if (!kstack) {
                goto get_kstack_error;
        }
        thread->kstack_bottom =
                (vaddr)kstack + thread_kstack_page_num * PAGE_SIZE;
        memset(kstack, '\0', thread_kstack_page_num * PAGE_SIZE);
        arch_set_new_thread_ctx(&(thread->ctx),
                                (void*)(thread_entry),
                                kstack + thread_kstack_page_num * PAGE_SIZE);
        /*
        set the init parameter of the thread
        the parameter must no more then the NR_ABI_PARAMETER_INT_REG
        and must all are integer, otherwise more parameters will be ignore
        */
        for (int i = 0; i < nr_parameter && i < NR_ABI_PARAMETER_INT_REG; i++) {
                /*here we think in rendezvos kernel,we only use the int
                 * parameters*/
                thread->init_parameter->int_para[i] = va_arg(arg_list, u64);
        }
        thread->init_parameter->thread_func_ptr = __func;
        va_end(arg_list);
        return thread;
get_kstack_error:
        del_thread_structure(thread);
new_thread_structure_error:
        return NULL;
}
void delete_thread(Thread_Base* thread)
{
        if (!thread || !thread->belong_tcb)
                return;
        atomic64_store(&thread->status, thread_status_exit);
        /*free the send pending msg*/
        Message_t* pending_msg = (Message_t*)atomic64_exchange(
                (volatile u64*)(&thread->send_pending_msg), (u64)NULL);
        if (pending_msg) {
                ref_put(&pending_msg->ms_queue_node.refcount, free_message_ref);
        }
        /*clean the send msg queue*/
        clean_message_queue(&thread->send_msg_queue, true);
        /*clean the recv msg queue*/
        clean_message_queue(&thread->recv_msg_queue, true);

        error_t e = -E_RENDEZVOS;
        if (thread->kstack_bottom) {
                /*
                 * at some time,
                 * if you using the new_thread_structure and try to use
                 * delete_thread, the kstack bottom is 0,but not an error
                 */
                void* thread_stack_start = (void*)thread->kstack_bottom
                                           - thread->kstack_num * PAGE_SIZE;
                e = free_pages(thread_stack_start,
                               thread->kstack_num,
                               thread->belong_tcb->vs,
                               percpu(nexus_root));
                if (e) {
                        pr_error(
                                "[ Error ] free pages fail please check the parameters\n");
                }
        }
        e = del_thread_from_task(thread);
        if (e) {
                pr_error(
                        "[ Error ] delete thread from task fail, please check\n");
        }
        e = del_thread_from_manager(thread);
        if (e) {
                pr_error(
                        "[ Error ] delete thread from manager fail,please check\n");
        }
        ref_put(&thread->refcount, free_thread_ref);
        return;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread) {
                return -E_IN_PARAM;
        }
        error_t res = 0;
        res = add_thread_to_task(task, thread);
        if (res)
                return res;
        res = add_thread_to_manager(percpu(core_tm), thread);
        thread_set_status(thread, thread_status_ready);
        return res;
}

static void thread_port_cache_invalidate_entry(struct thread_port_cache* c,
                                               u32 idx, Message_Port_t* port)
{
        c->entries[idx].port = NULL;
        c->entries[idx].lru_counter = 0;
        c->count--;
        ref_put(&port->refcount, free_message_port_ref);
}

static Message_Port_t* thread_port_cache_lookup(Thread_Base* thread,
                                                const char* name)
{
        struct thread_port_cache* c = &thread->port_cache;
        Message_Port_t* found_port = NULL;
        u32 found_idx = THREAD_MAX_KNOWN_PORTS;

        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                Message_Port_t* port = c->entries[i].port;
                if (!port)
                        continue;
                if (strcmp(port->name, name) != 0)
                        continue;
                found_port = port;
                found_idx = i;
                break;
        }

        if (found_port && found_port->registered) {
                if (!found_port->table) {
                        thread_port_cache_invalidate_entry(
                                c, found_idx, found_port);
                        return NULL;
                }

                if (global_port_table) {
                        struct spin_lock_t* my_lock =
                                &percpu(port_table_spin_lock);
                        lock_mcs(&global_port_table->lock, my_lock);
                        bool still_valid =
                                (found_port->table == global_port_table)
                                && found_port->registered;
                        unlock_mcs(&global_port_table->lock, my_lock);

                        if (!still_valid) {
                                thread_port_cache_invalidate_entry(
                                        c, found_idx, found_port);
                                return NULL;
                        }
                }

                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!c->entries[i].port)
                                continue;
                        if (i == found_idx) {
                                c->entries[i].lru_counter = 0;
                        } else {
                                c->entries[i].lru_counter++;
                        }
                }
                if (!ref_get_not_zero(&found_port->refcount)) {
                        found_port = NULL;
                }
        } else if (found_port) {
                thread_port_cache_invalidate_entry(c, found_idx, found_port);
                return NULL;
        }

        return found_port;
}

static error_t thread_port_cache_add(Thread_Base* thread, Message_Port_t* port)
{
        if (!port || !port->registered)
                return -E_IN_PARAM;

        if (!ref_get_not_zero(&port->refcount))
                return -E_RENDEZVOS;

        struct thread_port_cache* c = &thread->port_cache;

        if (c->count < THREAD_MAX_KNOWN_PORTS) {
                u32 empty_idx = THREAD_MAX_KNOWN_PORTS;
                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!c->entries[i].port) {
                                empty_idx = i;
                                break;
                        }
                }
                if (empty_idx >= THREAD_MAX_KNOWN_PORTS)
                        return -E_RENDEZVOS;

                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (c->entries[i].port && i != empty_idx) {
                                c->entries[i].lru_counter++;
                        }
                }
                c->entries[empty_idx].port = port;
                c->entries[empty_idx].lru_counter = 0;
                c->count++;
                return REND_SUCCESS;
        }

        u32 lru_idx = 0;
        u64 max_counter = 0;
        bool found = false;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                if (!c->entries[i].port)
                        continue;
                if (!found || c->entries[i].lru_counter > max_counter) {
                        max_counter = c->entries[i].lru_counter;
                        lru_idx = i;
                        found = true;
                }
        }
        if (!found)
                return -E_RENDEZVOS;

        Message_Port_t* evicted = c->entries[lru_idx].port;
        if (evicted)
                ref_put(&evicted->refcount, free_message_port_ref);

        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                if (c->entries[i].port && i != lru_idx) {
                        c->entries[i].lru_counter++;
                }
        }
        c->entries[lru_idx].port = port;
        c->entries[lru_idx].lru_counter = 0;

        return REND_SUCCESS;
}
/*You have to use ref put after finish use of this function*/
Message_Port_t* thread_lookup_port(const char* name)
{
        Thread_Base* self = get_cpu_current_thread();
        if (!self || !name || !global_port_table)
                return NULL;

        Message_Port_t* port = thread_port_cache_lookup(self, name);
        if (port) {
                if (!ref_get_not_zero(&port->refcount)) {
                        return NULL;
                }
                return port;
        }

        port = port_table_lookup(global_port_table, name);
        if (!port)
                return NULL;

        if (thread_port_cache_add(self, port) != REND_SUCCESS) {
                ref_put(&port->refcount, free_message_port_ref);
                return NULL;
        }

        return port;
}