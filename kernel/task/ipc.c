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
Thread_Base* ipc_port_try_match(Message_Port_t* port, u16 my_state)
{
        u16 target_type = (my_state == IPC_ENDPOINT_STATE_SEND) ?
                                  IPC_ENDPOINT_STATE_RECV :
                                  IPC_ENDPOINT_STATE_SEND;
        tagged_ptr_t dequeued_ptr =
                msq_dequeue_check_head(&port->thread_queue, target_type);
        if (tp_is_none(dequeued_ptr)) {
                return NULL;
        }
        ms_queue_node_t* dequeued_node =
                (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
        Thread_Base* opposite_thread = container_of(
                tp_get_ptr(dequeued_node->next), Thread_Base, port_queue_node);
        return opposite_thread;
}
error_t ipc_port_enqueue_wait(Message_Port_t* port, u16 my_state,
                              Thread_Base* my_task)
{
        u8 queue_state = ipc_get_queue_state(port);
        u64 expected_state;
        if (queue_state == IPC_ENDPOINT_STATE_EMPTY) {
                expected_state = IPC_ENDPOINT_STATE_EMPTY;
        } else if (queue_state == my_state) {
                expected_state = my_state;
        } else {
                /*the state is wrong,and let the caller retry*/
                return -E_REND_AGAIN;
        }
        return msq_enqueue_check_tail(&port->thread_queue,
                                      &my_task->port_queue_node,
                                      my_state,
                                      expected_state);
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