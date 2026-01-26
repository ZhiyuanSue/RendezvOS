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
                message_structure_ref_inc(msg);
                msg->append_info_len = append_info_len;
                msg->msg_type = msg_type;
                memcpy(&msg->append_info, append_info, append_info_len);
        }
        return msg;
}
void delete_message_structure(Message_t* msg)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, (void*)msg);
}
void message_structure_ref_inc(Message_t* msg)
{
        atomic64_inc(&msg->ref_count);
        barrier();
}
bool message_structure_ref_dec(Message_t* msg)
{
        i64 old_ref_value = atomic64_fetch_dec(&msg->ref_count);
        if (old_ref_value == 1) {
                delete_message_structure(msg);
                return true;
        }
        return false;
}
void clean_message_queue(ms_queue_t* ms_queue)
{
        tagged_ptr_t dequeued_ptr;
        Message_t* clean_msg_ptr;
        while (!tp_is_none(dequeued_ptr = msq_dequeue(ms_queue))) {
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                clean_msg_ptr =
                        container_of(dequeued_node, Message_t, msg_queue_node);
                message_structure_ref_dec(clean_msg_ptr);
        }
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
                atomic64_store((volatile u64*)&opposite_thread->port_ptr,
                               (u64)NULL);
                if (thread_get_status(opposite_thread) == thread_status_exit
                    || thread_get_status(opposite_thread)
                               == thread_status_cancel_ipc) {
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
        atomic64_store((volatile u64*)&my_thread->port_ptr, (u64)port);
        barrier();
        error_t ret = msq_enqueue_check_tail(&port->thread_queue,
                                             &my_thread->port_queue_node,
                                             my_ipc_state,
                                             expected_ipc_state);
        if (ret != REND_SUCCESS) {
                thread_structure_ref_dec(my_thread);
                atomic64_store((volatile u64*)&my_thread->port_ptr, (u64)NULL);
        }
        return ret;
}
/**
 * @brief when we get the opposite thread, we need to send the message, here we
 * get a message from sender and put it to the recv's recv queue
 * @param sender , the sender thread
 * @param receiver, the rece thread
 * @return , REND_SUCCESS, successfully put a sender's message into receiver's
 * recv queue, E_REND_NO_MSG, no message from sender need to sendto(here the
 * receiver might retry)
 */
error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver)
{
        tagged_ptr_t dequeued_ptr;
        Message_t* tmp_msg_ptr;
        if (!sender->send_pending_msg) {
                dequeued_ptr = msq_dequeue(&sender->send_msg_queue);
                if (tp_is_none(dequeued_ptr)) {
                        return -E_REND_NO_MSG;
                }
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                sender->send_pending_msg =
                        container_of(dequeued_node, Message_t, msg_queue_node);
        }
        tmp_msg_ptr = (Message_t*)sender->send_pending_msg;
        sender->send_pending_msg = NULL;
        barrier();

        msq_enqueue(&receiver->recv_msg_queue, &tmp_msg_ptr->msg_queue_node, 0);
        i64 new_cnt = atomic64_fetch_add(&receiver->recv_pending_cnt, 1) + 1;
        /*TODO: do some ops based on new cnt*/
        (void)new_cnt;
        /*TODO：set the sender and receiver's status*/

        return REND_SUCCESS;
}
/**
 * @brief we try to send a message, before this step , we must have put the
 * message into the send queue, and this function only make sure if there's a
 * receiver ,we can send a message from our send queue ,otherwise ,we are
 * blocked
 * @param port, the port that we find the opposite thread
 * @return. the value means the send status, if REND_SUCCESS, a message have
 * sent. others, the reason we fail
 */
error_t send_msg(Message_Port_t* port)
{
        Thread_Base* send_thread = get_cpu_current_thread();
        /*TODO*/
        (void)port;
        (void)send_thread;
        return REND_SUCCESS;
}
/**
 * @brief we try to receive a message, after this step, we need try to dequeue a
 * message from the recv queue, this function only make sure we have receive a
 * message
 * @param port, the port that we find the opposite thread
 * @return. the value means the send status, if REND_SUCCESS, a message have
 * receive. others, the reason we fail
 */
error_t recv_msg(Message_Port_t* port)
{
        Thread_Base* recv_thread = get_cpu_current_thread();
        /*TODO*/
        (void)port;
        (void)recv_thread;
        return REND_SUCCESS;
}