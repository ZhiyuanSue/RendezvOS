#include <rendezvos/task/ipc.h>

struct Msg_Port* create_message_port()
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg_Port* mp = cpu_kallocator->m_alloc(cpu_kallocator,
                                                      sizeof(struct Msg_Port));

        if (mp) {
                mp->thread_queue_dummy_node_ptr =
                        (Thread_Base*)(cpu_kallocator->m_alloc(
                                cpu_kallocator, sizeof(Thread_Base)));
                Message_t* dummy_msg_queue_node =
                        (Message_t*)(cpu_kallocator->m_alloc(
                                cpu_kallocator, sizeof(Message_t)));
                /*TODO: maybe need to init the header idle msg and the header
                 * tcb*/
                if (mp->thread_queue_dummy_node_ptr) {
                        msq_init(&mp->thread_queue,
                                 &mp->thread_queue_dummy_node_ptr
                                          ->port_queue_node,
                                 IPC_ENDPOINT_APPEND_BITS);
                }

                if (dummy_msg_queue_node)
                        msq_init(&mp->send_msg_queue,
                                 &dummy_msg_queue_node->msg_queue_node,
                                 IPC_ENDPOINT_APPEND_BITS);
        } else {
                return NULL;
        }
        return mp;
}
void delete_message_port(Message_Port_t* port)
{
        percpu(kallocator)
                ->m_free(percpu(kallocator), port->thread_queue_dummy_node_ptr);
        percpu(kallocator)->m_free(percpu(kallocator), port);
}
struct Msg* create_message(i64 msg_type, u64 append_info_len, char* append_info)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg* msg = cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(struct Msg) + append_info_len);
        if (msg) {
                msg->append_info_len = append_info_len;
                msg->msg_type = msg_type;
                memcpy(&msg->append_info, append_info, append_info_len);
        }
        return msg;
}
/**
 * @brief try to get a thread from the port
 * @param port the port we need to send to or recv from
 * @param my_ipc_state this function can be used for both send and recv, so need
 * to give the current op is send or recv
 * @return get the opposite thread or a null ptr(if fail), if you successfully
 * get the thread, you need to dec the ref count of the opposite thread
 * structure
 */
Thread_Base* ipc_port_try_match(Message_Port_t* port, u16 my_ipc_state)
{
        u16 target_ipc_state = (my_ipc_state == IPC_ENDPOINT_STATE_SEND) ?
                                       IPC_ENDPOINT_STATE_RECV :
                                       IPC_ENDPOINT_STATE_SEND;
        while (1) {
                tagged_ptr_t dequeued_ptr = msq_dequeue_check_head(
                        &port->thread_queue, target_ipc_state);
                if (tp_is_none(dequeued_ptr)) {
                        return NULL;
                }
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                Thread_Base* opposite_thread =
                        container_of(tp_get_ptr(dequeued_node->next),
                                     Thread_Base,
                                     port_queue_node);
                barrier();
                /*if we find the opposite_thread is dead ,dec the ref count(may
                 * be free the structure)*/
                atomic64_store(&opposite_thread->port_ptr, (u64)NULL);
                if (thread_get_status(opposite_thread) == thread_status_exit) {
                        thread_structure_ref_dec(opposite_thread);
                        continue;
                }
                return opposite_thread;
        }
}
/**
 * @brief try to block and enqueue my thread on the port
 * @param port the port we need to send to or recv from
 * @param my_ipc_state this function can be used for both send and recv, so need
 * to give the current op is send or recv
 * @param my_thread my thread control block
 * @return REND_SUCCESS successfully enqueue, E_REND_AGAIN, fail to enqueue and
 * please retry(maybe port ipc state have changed )
 */
error_t ipc_port_enqueue_wait(Message_Port_t* port, u16 my_ipc_state,
                              Thread_Base* my_thread)
{
        u8 queue_ipc_state = ipc_get_queue_state(port);
        u64 expected_ipc_state;
        if (queue_ipc_state == IPC_ENDPOINT_STATE_EMPTY) {
                expected_ipc_state = IPC_ENDPOINT_STATE_EMPTY;
        } else if (queue_ipc_state == my_ipc_state) {
                expected_ipc_state = my_ipc_state;
        } else {
                /*the state is wrong,and let the caller retry*/
                return -E_REND_AGAIN;
        }
        /*set my thread structure also belong to the port, and set the
         * port_ptr*/
        thread_structure_ref_inc(my_thread);
        atomic64_store(&my_thread->port_ptr, (u64)port);
        barrier();
        error_t ret = msq_enqueue_check_tail(&port->thread_queue,
                                             &my_thread->port_queue_node,
                                             my_ipc_state,
                                             expected_ipc_state);
        if (ret != REND_SUCCESS) {
                thread_structure_ref_dec(my_thread);
                atomic64_store(&my_thread->port_ptr, (u64)NULL);
        }
        return ret;
}
error_t send_msg(Message_Port_t* port, Message_t* message)
{
        (void)message;
        Thread_Base* send_thread = get_cpu_current_thread();
        while (1) {
                u16 tcb_queue_state = ipc_get_queue_state(port);
                switch (tcb_queue_state) {
                case IPC_ENDPOINT_STATE_EMPTY:
                case IPC_ENDPOINT_STATE_SEND:
                        u64 expected_tail_state =
                                (tcb_queue_state == IPC_ENDPOINT_STATE_EMPTY) ?
                                        IPC_ENDPOINT_STATE_EMPTY :
                                        IPC_ENDPOINT_STATE_SEND;
                        if (msq_enqueue_check_tail(&port->thread_queue,
                                                   &send_thread->port_queue_node,
                                                   IPC_ENDPOINT_STATE_SEND,
                                                   expected_tail_state)
                            == 0) {
                                /*TODO: maybe need schedule*/

                                return REND_SUCCESS;
                        }
                        break;
                case IPC_ENDPOINT_STATE_RECV:
                        tagged_ptr_t dequeued_ptr = msq_dequeue_check_head(
                                &port->thread_queue, IPC_ENDPOINT_STATE_RECV);
                        if (tp_is_none(dequeued_ptr)) {
                                break;
                        }

                        ms_queue_node_t* dequeued_node =
                                (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                        /*
                        here, because we use msq_dequeue_check_head,
                        and dequeued_ptr is not none,
                        then the dequeued_node->next must exist
                        */
                        Thread_Base* recv_ipc_node =
                                container_of(tp_get_ptr(dequeued_node->next),
                                             Thread_Base,
                                             port_queue_node);
                        /*put the msg on recv ipc node's recv_msg_queue*/

                        /*TODO:send message*/
                        (void)recv_ipc_node;

                        /*TODO:maybe need to schedule*/

                        return REND_SUCCESS;

                default:
                        /*should not achieve here*/
                        arch_cpu_relax();
                        continue;
                }
        }
}
Message_t* recv_msg(Message_Port_t* port)
{
        Thread_Base* recv_thread = get_cpu_current_thread();
        while (1) {
                u16 tcb_queue_state = ipc_get_queue_state(port);
                switch (tcb_queue_state) {
                case IPC_ENDPOINT_STATE_EMPTY:
                case IPC_ENDPOINT_STATE_RECV:
                        u64 expected_tail_state =
                                (tcb_queue_state == IPC_ENDPOINT_STATE_EMPTY) ?
                                        IPC_ENDPOINT_STATE_EMPTY :
                                        IPC_ENDPOINT_STATE_RECV;
                        if (msq_enqueue_check_tail(&port->thread_queue,
                                                   &recv_thread->port_queue_node,
                                                   IPC_ENDPOINT_STATE_RECV,
                                                   expected_tail_state)
                            == 0) {
                                /*TODO: maybe need schedule*/

                                return REND_SUCCESS;
                        }
                        break;
                case IPC_ENDPOINT_STATE_SEND:
                        tagged_ptr_t dequeued_ptr = msq_dequeue_check_head(
                                &port->thread_queue, IPC_ENDPOINT_STATE_SEND);
                        if (tp_is_none(dequeued_ptr)) {
                                break;
                        }
                        ms_queue_node_t* dequeued_node =
                                (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                        /*
                        here, because we use msq_dequeue_check_head,
                        and dequeued_ptr is not none,
                        then the dequeued_node->next must exist
                        */
                        Thread_Base* send_ipc_node =
                                container_of(tp_get_ptr(dequeued_node->next),
                                             Thread_Base,
                                             port_queue_node);
                        /*pop a message from send message queue and put it one
                         * recv thread's recv queue*/

                        /*TODO:...*/
                        (void)send_ipc_node;

                        /*TODO:maybe need to schedule*/

                        return REND_SUCCESS;
                        break;
                default:
                        /*should not achieve here*/
                        arch_cpu_relax();
                        continue;
                }
        }
}
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg_await(Message_Port_t* port);