#include <rendezvos/task/ipc.h>
#include <common/string.h>

struct Msg_Port* create_message_port()
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg_Port* mp = cpu_kallocator->m_alloc(cpu_kallocator,
                                                      sizeof(struct Msg_Port));

        if (mp) {
                mp->thread_queue_dummy_node_ptr =
                        (Thread_Base*)(cpu_kallocator->m_alloc(
                                cpu_kallocator, sizeof(Thread_Base)));
                /* Dummy must be zeroed so free_thread_ref (via
                 * del_thread_structure) does not dereference garbage
                 * init_parameter when the dummy is dequeued and freed by
                 * msq_dequeue_check_head. */
                if (mp->thread_queue_dummy_node_ptr) {
                        memset(mp->thread_queue_dummy_node_ptr,
                               0,
                               sizeof(Thread_Base));
                        msq_init(
                                &mp->thread_queue,
                                &mp->thread_queue_dummy_node_ptr->ms_queue_node,
                                IPC_ENDPOINT_APPEND_BITS);
                }
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
                tagged_ptr_t dequeued_ptr =
                        msq_dequeue_check_head(&port->thread_queue,
                                               MSQ_CHECK_FIELD_APPEND,
                                               tp_new(NULL, target_ipc_state),
                                               NULL);
                if (tp_is_none(dequeued_ptr)) {
                        return NULL;
                }
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                Thread_Base* opposite_thread =
                        container_of(dequeued_node, Thread_Base, ms_queue_node);
                barrier();
                /*if we find the opposite_thread is dead ,dec the ref count(may
                 * be free the structure)*/
                if (thread_get_status(opposite_thread) == thread_status_exit) {
                        ref_put(&dequeued_node->refcount, free_thread_ref);
                        continue;
                }
                if (thread_set_status_with_expect(opposite_thread,
                                                  thread_status_cancel_ipc,
                                                  thread_status_ready)) {
                        ref_put(&dequeued_node->refcount, free_thread_ref);
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
        /* Thread has refcount=1 from create_thread; refcount_is_zero=false to
         * avoid revival risk (ref_get_not_zero instead of ref_get_claim). */

        barrier();
        error_t ret = msq_enqueue_check_tail(&port->thread_queue,
                                             &my_thread->ms_queue_node,
                                             my_ipc_state,
                                             tp_new(NULL, expected_ipc_state),
                                             free_thread_ref,
                                             false /* refcount_is_zero */);
        return ret;
}
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

         - Problem 4:
        The msq queue is designed to the dequeued is old dummy node,
        and it's next is the real data node, which now as the new dummy
        node. but we cannot directly put real data node to receive queue.

        To fix problem 4, I have to copy it to a new message structure. And put
        the new message structure to receiver queue.

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
                dequeued_ptr =
                        msq_dequeue(&sender->send_msg_queue, free_message_ref);
                if (tp_is_none(dequeued_ptr)) {
                        return -E_REND_NO_MSG;
                }
                send_msg_ptr = container_of(
                        tp_get_ptr(dequeued_ptr), Message_t, ms_queue_node);
                if (thread_get_status(sender) == thread_status_exit) {
                        ref_put(&send_msg_ptr->ms_queue_node.refcount,
                                free_message_ref);
                        return -E_REND_NO_MSG;
                }
        }

        /*after we get the send msg, we only need to consider the receiver is
         * exiting problem. If the sender is running this function, it's a
         * problem, but if the receiver is running this funciton, the receiver
         * must be alive*/

        /* first try to give this message to the receiver and add the ref count
         * of this message, which means now the receiver and the
         * send_pending_msg all have this message*/
        Message_t* recv_msg_ptr = create_message(send_msg_ptr->data);
        atomic64_add(&receiver->recv_pending_cnt, 1);
        /* recv_msg_ptr from create_message has refcount=1;
         * refcount_is_zero=false */
        msq_enqueue(&receiver->recv_msg_queue,
                    &recv_msg_ptr->ms_queue_node,
                    free_message_ref,
                    false /* refcount_is_zero */);

        /*check whether the receiver is alive*/
        if (thread_get_status(receiver) == thread_status_exit) {
                /*
                The upper function should handle this try again
                return.
                */
                /*try to return the owner from send_msg_ptr to
                 * sender->send_pending_msg*/
                int retry_count = 0;
                while (atomic64_cas((volatile u64*)(&sender->send_pending_msg),
                                    (u64)NULL,
                                    (u64)send_msg_ptr)
                       != (u64)NULL) {
                        arch_cpu_relax();
                        retry_count++;
                        if (retry_count == 100) {
                                thread_set_status(sender,
                                                  thread_status_block_on_send);
                                schedule(percpu(core_tm));
                                retry_count = 0;
                        }
                }
                /*no need to ref put the send msg ptr, it just change the owner
                 * to sender->send_pending_msg*/
                send_msg_ptr = NULL;
                return -E_REND_AGAIN;
        }
        /*now the send_msg_ptr should not hold this message*/
        ref_put(&send_msg_ptr->ms_queue_node.refcount, free_message_ref);
        send_msg_ptr = NULL;
        barrier();

        return REND_SUCCESS;
}
error_t send_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* sender = get_cpu_current_thread();
        while (1) {
                Thread_Base* receiver =
                        ipc_port_try_match(port, IPC_ENDPOINT_STATE_SEND);
                if (receiver) {
                        error_t try_transfer_result =
                                ipc_transfer_message(sender, receiver);
                        ref_put(&receiver->ms_queue_node.refcount,
                                free_thread_ref);
                        switch (try_transfer_result) {
                        case REND_SUCCESS: {
                                /*successfully transfer the message*/
                                /*need to change the receiver's status*/
                                thread_set_status_with_expect(
                                        receiver,
                                        thread_status_block_on_receive,
                                        thread_status_ready);
                                return REND_SUCCESS;
                        }
                        case -E_REND_NO_MSG: {
                                /*impossible, the ipc_send function find self
                                 * have no message or is exiting */
                                return -E_RENDEZVOS;
                        }
                        case -E_REND_AGAIN: {
                                /*the receiver have exit, we need try to dequeue
                                 * a receiver again.*/
                                continue;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                return -E_RENDEZVOS;
                        }
                        }
                } else {
                        error_t enqueue_result = ipc_port_enqueue_wait(
                                port, IPC_ENDPOINT_STATE_SEND, sender);
                        switch (enqueue_result) {
                        case REND_SUCCESS: {
                                /*successfully enqueue on the port queue, need
                                 * schedule; when we are woken up, a receiver
                                 * has matched us and transferred the message*/
                                thread_set_status(sender,
                                                  thread_status_block_on_send);
                                schedule(percpu(core_tm));
                                return REND_SUCCESS;
                        }
                        case -E_REND_AGAIN: {
                                /*the queue have change the state, we need
                                 * retry.*/
                                continue;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                return -E_RENDEZVOS;
                        }
                        }
                }
        }
        return REND_SUCCESS;
}
error_t recv_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* receiver = get_cpu_current_thread();
        while (1) {
                Thread_Base* sender =
                        ipc_port_try_match(port, IPC_ENDPOINT_STATE_RECV);
                if (sender) {
                        error_t try_transfer_result =
                                ipc_transfer_message(sender, receiver);
                        ref_put(&sender->ms_queue_node.refcount,
                                free_thread_ref);
                        switch (try_transfer_result) {
                        case REND_SUCCESS: {
                                /*successfully receive a message*/
                                thread_set_status_with_expect(
                                        sender,
                                        thread_status_block_on_send,
                                        thread_status_ready);
                                return REND_SUCCESS;
                        }
                        case -E_REND_NO_MSG: {
                                /*the sender have no message to send or is
                                 * exiting, retry to get another sender*/
                                continue;
                        }
                        case -E_REND_AGAIN: {
                                /*impossible, only receiver is exiting can
                                 * return -E_REND_AGAIN*/
                                return -E_RENDEZVOS;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                return -E_RENDEZVOS;
                        }
                        }
                } else {
                        error_t enqueue_result = ipc_port_enqueue_wait(
                                port, IPC_ENDPOINT_STATE_RECV, receiver);
                        switch (enqueue_result) {
                        case REND_SUCCESS: {
                                /*successfully enqueue on the port queue; when
                                 * we are woken up, a sender has matched us and
                                 * transferred the message*/
                                thread_set_status(
                                        receiver,
                                        thread_status_block_on_receive);
                                schedule(percpu(core_tm));
                                return REND_SUCCESS;
                        }
                        case -E_REND_AGAIN: {
                                /*the queue have change the state, we need
                                 * retry.*/
                                continue;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                return -E_RENDEZVOS;
                        }
                        }
                }
        }

        return REND_SUCCESS;
}

error_t enqueue_msg_for_send(Message_t* msg, bool refcount_is_zero)
{
        if (!msg)
                return -E_IN_PARAM;
        Thread_Base* self = get_cpu_current_thread();
        if (!self)
                return -E_REND_AGAIN;
        if (!ref_count(&msg->ms_queue_node.refcount)
            || (msg->data && !ref_count(&msg->data->refcount))) {
                return -E_REND_IPC;
        }
        msq_enqueue(&self->send_msg_queue,
                    &msg->ms_queue_node,
                    free_message_ref,
                    refcount_is_zero);
        return REND_SUCCESS;
}

Message_t* dequeue_recv_msg(void)
{
        Thread_Base* self = get_cpu_current_thread();
        if (!self)
                return NULL;
        tagged_ptr_t dp = msq_dequeue(&self->recv_msg_queue, NULL);
        if (tp_is_none(dp))
                return NULL;
        ms_queue_node_t* msg_node = (ms_queue_node_t*)tp_get_ptr(dp);
        return container_of(msg_node, Message_t, ms_queue_node);
}
error_t cancel_ipc(Thread_Base* target_thread)
{
        /*
        first we need to check the thread's status, only blocked on send/recv
        can we cancel the ipc. second, if the thread is the head of the waiting
        queue, we can remove it from the queue even there's no opposite thread
        try to remove it.
        */
        /*
        Remember:
        You need to check the thread's status and decide how to schedule after
        this function, only it's ready means the target thread's ipc have
        canceled
        */
        if (!target_thread) {
                return -E_IN_PARAM;
        }
        Message_Port_t* port =
                container_of(target_thread->ms_queue_node.queue_ptr,
                             Message_Port_t,
                             thread_queue);
        tagged_ptr_t dequeue_head_ptr = tp_new_none();
        if (thread_set_status_with_expect(target_thread,
                                          thread_status_block_on_send,
                                          thread_status_cancel_ipc)) {
                dequeue_head_ptr = msq_dequeue_check_head(
                        &port->thread_queue,
                        MSQ_CHECK_FIELD_PTR,
                        tp_new((void*)(&target_thread->ms_queue_node),
                               IPC_ENDPOINT_STATE_SEND),
                        NULL);

        } else if (thread_set_status_with_expect(target_thread,
                                                 thread_status_block_on_receive,
                                                 thread_status_cancel_ipc)) {
                dequeue_head_ptr = msq_dequeue_check_head(
                        &port->thread_queue,
                        MSQ_CHECK_FIELD_PTR,
                        tp_new((void*)(&target_thread->ms_queue_node),
                               IPC_ENDPOINT_STATE_RECV),
                        NULL);
        }
        if (!tp_is_none(dequeue_head_ptr)) {
                ref_put(&((ms_queue_node_t*)tp_get_ptr(dequeue_head_ptr))
                                 ->refcount,
                        NULL);
                if (!thread_set_status_with_expect(target_thread,
                                                   thread_status_cancel_ipc,
                                                   thread_status_ready)) {
                        return -E_RENDEZVOS;
                }
        }
        return REND_SUCCESS;
}