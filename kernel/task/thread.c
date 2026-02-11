#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
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
Thread_Base* new_thread_structure(struct allocator* cpu_allocator)
{
        if (!cpu_allocator)
                return NULL;
        Thread_Base* thread = (Thread_Base*)(cpu_allocator->m_alloc(
                cpu_allocator, sizeof(Thread_Base)));
        if (thread) {
                memset((void*)thread, 0, sizeof(Thread_Base));
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
                thread->init_parameter = new_init_parameter_structure();
                thread->flags = THREAD_FLAG_NONE;

                /*ipc part*/
                Message_t* dummy_recv_msg_node =
                        (Message_t*)(cpu_allocator->m_alloc(cpu_allocator,
                                                            sizeof(Message_t)));
                Message_t* dummy_send_msg_node =
                        (Message_t*)(cpu_allocator->m_alloc(cpu_allocator,
                                                            sizeof(Message_t)));
                if (dummy_recv_msg_node) {
                        memset(dummy_recv_msg_node, 0, sizeof(Message_t));
                        msq_init(&thread->recv_msg_queue,
                                 &dummy_recv_msg_node->ms_queue_node,
                                 0);
                }
                if (dummy_send_msg_node) {
                        memset(dummy_send_msg_node, 0, sizeof(Message_t));
                        msq_init(&thread->send_msg_queue,
                                 &dummy_send_msg_node->ms_queue_node,
                                 0);
                }

                thread->send_pending_msg = NULL;
                thread->recv_pending_cnt.counter = 0;
                thread->port_ptr = NULL;
        }
        return thread;
}
void del_thread_structure(Thread_Base* thread)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!thread || !cpu_allocator)
                return;
        /*first free the init parameter*/
        del_init_parameter_structure(thread->init_parameter);
        cpu_allocator->m_free(cpu_allocator, thread);
}
void free_thread_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        ms_queue_node_t* node =
                container_of(ref_count_ptr, ms_queue_node_t, refcount);
        Thread_Base* thread = container_of(node, Thread_Base, ms_queue_node);
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
Thread_Base* create_thread(void* __func, int nr_parameter, ...)
{
        Thread_Base* thread = new_thread_structure(percpu(kallocator));
        ref_get_claim(&thread->ms_queue_node.refcount);
        thread->tid = get_new_tid();
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
}
void delete_thread(Thread_Base* thread)
{
        if (!thread)
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

        if (thread->kstack_bottom) {
                /*
                 * at some time,
                 * if you using the new_thread_structure and try to use
                 * delete_thread, the kstack bottom is 0,but not an error
                 */
                void* thread_stack_start = (void*)thread->kstack_bottom
                                           - thread->kstack_num * PAGE_SIZE;
                free_pages(thread_stack_start,
                           thread->kstack_num,
                           thread->belong_tcb->vs,
                           percpu(nexus_root));
        }
        del_thread_from_task(thread->belong_tcb, thread);
        del_thread_from_manager(thread);
        ref_put(&thread->ms_queue_node.refcount, free_thread_ref);
        return;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread)
{
        error_t res = 0;
        res = add_thread_to_task(task, thread);
        if (res)
                return res;
        res = add_thread_to_manager(percpu(core_tm), thread);
        thread_set_status(thread, thread_status_ready);
        return res;
}