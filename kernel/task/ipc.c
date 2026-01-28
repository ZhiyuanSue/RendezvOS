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
 * get a message from sender and put it to the recv's recv queue, if you want to
 * use this funciton ,you must make sure you have get the ref count of opposite
 * thread
 * @param sender , the sender thread
 * @param receiver, the rece thread
 * @return , REND_SUCCESS, successfully put a sender's message into receiver's
 * recv queue, E_REND_NO_MSG, no message from sender need to sendto(here the
 * receiver might retry)
 */
error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver)
{
        tagged_ptr_t dequeued_ptr;
        Message_t* send_msg_ptr;
        /*
         - Problem 1:
        if the sender try to send, but we find the receiver is exiting, a
        message is dequeued, but retry logic is on the upper level code, so the
        dequeued message must be reserved, otherwise this message is dropped.

        To fix problem 1, we use a sender->send_pending_msg, which record the
        pending message —— a message dequeued ,but not rightly sent to the
        receiver, the upper code must retry run ipc_transfer_message, and this
        time we use the send_pending_msg.
        (it's the sender push case!!! if the receiver pull, the receiver must
        exist, there's no possible that the receiver is exiting.)

         - Problem 2:
        if the sender is exiting, and now the receiver try to transfer, the
        sender might set the sender->send_pending_msg to NULL, it might have
        problem.

        To fix problem 2, we use send_msg_ptr, which is a ptr on the
        ipc_transfer_message function stack. After the send_msg_ptr get the
        message ptr from the sender->send_pending_msg, the sender can put it to
        NULL(exiting), and this message can still be sent to the receiver.
        (it's the receiver pull case!!! Here the sender is exiting is
        the problem reason)

         - Problem 3:
        there might have more then one receiver here, but we have only one
        send_pending_ptr, which might leading to some race condition

        so here we must first try get send_msg_ptr from
        sender->send_pending_msg(using atomic exchange).
        If not null, we have get the send message ptr, which means tell other
        receiver(if have) we have get message.
        If null ,means we must dequeue a message from the send_msg_queue. we
        needn't try to change the send_pending_msg ptr before we try to enqueue
        the receiver's recv_msg_queue.
        the ref count of this message should not change, but the owner have
        changed from sender (send_pending_msg or send_msg_queue) to send_msg_ptr

        After we try to send to receiver and find this message transfer fail, we
        need to using while and atomic64_cas try to change the send_pending_msg
        using send_msg_ptr when it's null.(it's sender push case, the receiver
        pull case have no such problem). The owner of this message change from
        send_msg_ptr to sender->send_pending_msg

        Remember, it's in kernel, no timer interrupt, so if this while happen,
        means a message have take the send_pending_msg ptr, and we try to put
        another messsage on this ptr, we have to wait, and no more receiver try
        to get this message, this while will not break. So, we must set the
        sender thread's status and schedule after several tries fail.

        No need to consider sender is exiting and transfer message fail case——if
        so, who is running this function? so it's impossible

        if we find this message transfer success. we also need to set
        send_msg_ptr to NULL and dec the ref count of message, because the
        send_msg_ptr will no longer take the message

        */
        if ((send_msg_ptr = (Message_t*)atomic64_exchange(
                     (volatile u64*)(&sender->send_pending_msg), (u64)NULL))
            == NULL) {
                dequeued_ptr = msq_dequeue(&sender->send_msg_queue);
                if (tp_is_none(dequeued_ptr)) {
                        return -E_REND_NO_MSG;
                }
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                send_msg_ptr =
                        container_of(dequeued_node, Message_t, msg_queue_node);
                if (thread_get_status(sender) == thread_status_exit) {
                }
        }

        /*after we get the send msg, we only need to consider the receiver is
         * exiting problem. If the sender is running this function, it's a
         * problem, but if the receiver is running this funciton, the receiver
         * must be alive*/

        /* first try to give this message to the receiver and add the ref count
         * of this message, which means now the receiver and the
         * send_pending_msg all have this message*/
        msq_enqueue(
                &receiver->recv_msg_queue, &send_msg_ptr->msg_queue_node, 0);
        message_structure_ref_inc(send_msg_ptr);
        i64 new_cnt = atomic64_fetch_add(&receiver->recv_pending_cnt, 1) + 1;

        /*check whether the receiver is alive*/
        if (thread_get_status(receiver) == thread_status_exit) {
                /*
                if not ,the receiver will clean the message ref count.
                if the receiver is exiting, the delete thread function will dec
                the ref count, but the sender still heve this message. So we
                don't need dec the ref count(have given to the receiver) here.
                The upper function should handle this try again return.
                */
                /*try to return the owner from send_msg_ptr to
                 * sender->send_pending_msg*/
                thread_set_status(thread_status_block_on_send, sender);
                int retry_count = 0;
                while (atomic64_cas((volatile u64*)(&sender->send_pending_msg),
                                    (u64)NULL,
                                    (u64)send_msg_ptr)
                       != (u64)NULL) {
                        arch_cpu_relax();
                        retry_count++;
                        if (retry_count == 100) {
                                schedule(percpu(core_tm));
                                retry_count = 0;
                        }
                }
                send_msg_ptr = NULL;
                return -E_REND_AGAIN;
        }
        /*now the send_msg_ptr should not hold this message*/
        send_msg_ptr = NULL;
        message_structure_ref_dec(send_msg_ptr);
        barrier();

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