#include <rendezvos/ipc/ipc.h>
#include <rendezvos/task/ebr.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/smp/percpu.h>
#include <modules/log/log.h>

static error_t free_ipc_request_real(ref_count_t* refcount)
{
        if (!refcount)
                return -E_IN_PARAM;
        ms_queue_node_t* node =
                container_of(refcount, ms_queue_node_t, refcount);
        Ipc_Request_t* req = container_of(node, Ipc_Request_t, ms_queue_node);
        delete_ipc_request(req);
        return REND_SUCCESS;
}

Ipc_Request_t* create_ipc_request(Thread_Base* thread)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Ipc_Request_t* req =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Ipc_Request_t));
        if (!req || !thread)
                return NULL;
        ref_init(&req->ms_queue_node.refcount);
        /* Pair with delete_ipc_request's ref_put on thread->refcount. */
        if (!ref_get_not_zero(&thread->refcount)) {
                cpu_kallocator->m_free(cpu_kallocator, req);
                return NULL;
        }
        req->thread = thread;
        return req;
}

void delete_ipc_request(Ipc_Request_t* req)
{
        if (!req)
                return;
        struct allocator* cpu_kallocator = percpu(kallocator);

        if (req->thread)
                ref_put(&req->thread->refcount, free_thread_ref);
        cpu_kallocator->m_free(cpu_kallocator, req);
}

/**
 * The matched oppo request have leave the wait queue with port_ptr cleared; but
 * transfer failed so nobody else will wake them. Flag + ready to wake them with
 * transfer fail(Empty Wake) → they return -E_REND_AGAIN.
 */
static void ipc_empty_wake_peer(Thread_Base* peer, u64 expect_status)
{
        if (!peer)
                return;
        thread_or_flags(peer, THREAD_FLAG_IPC_EMPTY_WAKE);
        (void)thread_set_status_with_expect(
                peer, expect_status, thread_status_ready);
}

error_t free_ipc_request(ref_count_t* refcount)
{
        if (!refcount)
                return -E_IN_PARAM;
        /*
         * Same as message nodes: request nodes are retired through EBR so that
         * lock-free queue readers cannot race with immediate free.
         */
        return ebr_retire_ref(refcount, free_ipc_request_real);
}

/*
 * Port close aborted a blocked send without transfer, and a payload may still
 * on send_pending_msg or in send_msg_queue(because before send ,a
 * enqueue_msg_for_send is called, and if the port closed, it only set the flag
 * and do not clean it). Drop the current message so the next send will not send
 * this msg.
 */
static void ipc_drop_one_orphan_send_msg(Thread_Base* sender)
{
        Message_t* msg;
        tagged_ptr_t dequeued_ptr;

        if (!sender)
                return;

        msg = (Message_t*)atomic64_exchange(
                (volatile u64*)&sender->send_pending_msg, (u64)NULL);
        if (!msg) {
                dequeued_ptr =
                        msq_dequeue(&sender->send_msg_queue, free_message_ref);
                if (tp_is_none(dequeued_ptr))
                        return;
                msg = container_of(
                        tp_get_ptr(dequeued_ptr), Message_t, ms_queue_node);
        }
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
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
Ipc_Request_t* ipc_port_try_match(Message_Port_t* port, u16 my_ipc_state)
{
        if (!port)
                return NULL;
        u16 target_ipc_state = (my_ipc_state == IPC_PORT_STATE_SEND) ?
                                       IPC_PORT_STATE_RECV :
                                       IPC_PORT_STATE_SEND;
        u64 target_thread_status = (my_ipc_state == IPC_PORT_STATE_SEND) ?
                                           thread_status_block_on_receive :
                                           thread_status_block_on_send;
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
                Ipc_Request_t* opposite_request = container_of(
                        dequeued_node, Ipc_Request_t, ms_queue_node);
                Thread_Base* opposite_thread = opposite_request->thread;
                /*if we find the opposite_thread is dead ,dec the ref count(may
                 * be free the structure)*/
                if (thread_get_status(opposite_thread)
                    != target_thread_status) {
                        atomic64_cas((volatile u64*)&opposite_thread->port_ptr,
                                     (u64)port,
                                     (u64)NULL);
                        ref_put(&dequeued_node->refcount, free_ipc_request);
                        continue;
                }
                u64 thread_port =
                        atomic64_cas((volatile u64*)&opposite_thread->port_ptr,
                                     (u64)port,
                                     (u64)NULL);
                if (thread_port != (u64)port) {
                        ref_put(&dequeued_node->refcount, free_ipc_request);
                        continue;
                }
                return opposite_request;
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
        if (!port || !my_thread)
                return -E_IN_PARAM;
        u8 queue_ipc_state = ipc_get_queue_state(port);
        u64 expected_ipc_state;
        error_t ret;
        if (queue_ipc_state == IPC_PORT_STATE_EMPTY) {
                expected_ipc_state = IPC_PORT_STATE_EMPTY;
        } else if (queue_ipc_state == my_ipc_state) {
                expected_ipc_state = my_ipc_state;
        } else {
                /*the state is wrong,and let the caller retry*/
                return -E_REND_AGAIN;
        }
        /* Thread has refcount=1 from create_thread; but after msq enqueue check
         * tail,we should not put ref of this thread */

        Ipc_Request_t* req = create_ipc_request(my_thread);
        if (!req) {
                return -E_REND_NO_MEM;
        }

        atomic64_store((volatile u64*)&req->queue_ptr,
                       (u64)(&port->thread_queue));
        if (atomic64_cas(
                    (volatile u64*)&my_thread->port_ptr, (u64)NULL, (u64)port)
            != (u64)NULL) {
                ret = -E_REND_AGAIN;
                goto enqueue_wait_out;
        }

        ret = msq_enqueue_check_tail(&port->thread_queue,
                                     &req->ms_queue_node,
                                     my_ipc_state,
                                     tp_new(NULL, expected_ipc_state),
                                     free_ipc_request);
        if (ret != REND_SUCCESS) {
                atomic64_store((volatile u64*)&my_thread->port_ptr, (u64)NULL);
        }
enqueue_wait_out:
        /*put the create refcount, now only queue hold one request*/
        ref_put(&req->ms_queue_node.refcount, free_ipc_request);
        return ret;
}
error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver)
{
        tagged_ptr_t dequeued_ptr;
        Message_t* send_msg_ptr;
        if (!sender || !receiver)
                return -E_IN_PARAM;

        if (thread_get_status(receiver) == thread_status_exit) {
                return -E_REND_AGAIN;
        }

        Message_t* recv_msg_ptr = create_message_structure();
        if (!recv_msg_ptr)
                return -E_REND_NO_MEM;
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
        node. but we cannot directly put real data node to receive queue, for
        it's still linked

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

        After we try to send to receiver and find this message transfer fail,
        maybe it's caused by no memory or the receiver is exiting. so we need to
        use senders' send_pending_msg ptr to store the dequeued but not
        successfully transfered msg. but if there's more then one receiver and
        they all find no memory, only one send_pending_msg will be a problem.

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
                        delete_message_structure(recv_msg_ptr);
                        return -E_REND_NO_MSG;
                }
                send_msg_ptr = container_of(
                        tp_get_ptr(dequeued_ptr), Message_t, ms_queue_node);
                if (thread_get_status(sender) == thread_status_exit) {
                        ref_put(&send_msg_ptr->ms_queue_node.refcount,
                                free_message_ref);
                        delete_message_structure(recv_msg_ptr);
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
        /* recv_msg_ptr from create_message_with_msg has refcount=1;
         * refcount_is_zero=false */
        if (fill_message_data(recv_msg_ptr, send_msg_ptr->data)
            != REND_SUCCESS) {
                /**
                 * The data is invalid, we have to clean
                 */
                ref_put(&send_msg_ptr->ms_queue_node.refcount,
                        free_message_ref);
                delete_message_structure(recv_msg_ptr);
                return -E_REND_NO_MSG;
        }
        msq_enqueue(&receiver->recv_msg_queue,
                    &recv_msg_ptr->ms_queue_node,
                    free_message_ref);
        ref_put(&recv_msg_ptr->ms_queue_node.refcount, free_message_ref);
        recv_msg_ptr = NULL;
        atomic64_add(&receiver->recv_pending_cnt, 1);

        if (thread_get_status(receiver) == thread_status_exit) {
                /*
                        Only sender push case can find the receiver is exiting
                        And receiver(maybe more then one) pull case should not
                   run into here The sender must only one, so just directly
                   change send_pending_ptr No Problem 3 case
                */
                if (atomic64_cas((volatile u64*)(&sender->send_pending_msg),
                                 (u64)NULL,
                                 (u64)send_msg_ptr)
                    != (u64)NULL) {
                        /*its impossible case, if so ,we drop this message
                         * directly*/
                        ref_put(&send_msg_ptr->ms_queue_node.refcount,
                                free_message_ref);
                        send_msg_ptr = NULL;
                        return -E_RENDEZVOS;
                }

                return -E_REND_AGAIN;
        }
        /*now the send_msg_ptr should not hold this message*/
        ref_put(&send_msg_ptr->ms_queue_node.refcount, free_message_ref);
        send_msg_ptr = NULL;
        return REND_SUCCESS;
}
error_t send_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* sender = get_cpu_current_thread();
        Ipc_Request_t* receiver_request = NULL;
        if (!sender) {
                return -E_RENDEZVOS;
        }
        if (!port_ops_begin(port)) {
                /*before there's a enqueue_msg_for_send, so we must try clean
                 * it, the same as ipc_try_send_msg*/
                ipc_drop_one_orphan_send_msg(sender);
                return -E_REND_PORT_CLOSED;
        }

        while (1) {
                if (!receiver_request) {
                        receiver_request =
                                ipc_port_try_match(port, IPC_PORT_STATE_SEND);
                }
                if (receiver_request) {
                        error_t try_transfer_result = ipc_transfer_message(
                                sender, receiver_request->thread);
                        switch (try_transfer_result) {
                        case REND_SUCCESS: {
                                /*successfully transfer the message*/
                                /*need to change the receiver's status*/
                                thread_set_status_with_expect(
                                        receiver_request->thread,
                                        thread_status_block_on_receive,
                                        thread_status_ready);
                                ref_put(&receiver_request->ms_queue_node
                                                 .refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return REND_SUCCESS;
                        }
                        case -E_REND_AGAIN: {
                                /*
                                 * Receiver is exiting. Drop this matched
                                 * request and try_match another RECV waiter.
                                 * Message is back on send_pending_msg.
                                 * Do not empty-wake: AGAIN may mean the
                                 * receiver already got a queue copy (exit
                                 * after enqueue); just continue and retry.
                                 */
                                ref_put(&receiver_request->ms_queue_node
                                                 .refcount,
                                        free_ipc_request);
                                receiver_request = NULL;
                                continue;
                        }
                        case -E_RENDEZVOS: {
                                /*
                                 * Impossible on sender-push (pending CAS fail
                                 * after recv already got a copy). Do not
                                 * empty-wake: peer may already hold the msg
                                 * (same reason as AGAIN). Msg was dropped in
                                 * transfer — return, do not rematch.
                                 */
                                ref_put(&receiver_request->ms_queue_node
                                                 .refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return -E_RENDEZVOS;
                        }
                        default: {
                                /* DO NOT CHANGE THE FOLLOWING COMMENTS:
                                 * For the following error code:
                                 * empty-wake then return.
                                 *
                                 * From ipc_transfer_message:
                                 * -E_REND_NO_MEM
                                 *     create_message_structure failed(OOM);
                                 *     untouch send queue/send_pending_ptr (msg
                                 *     still there). so it's standard empty-wake
                                 *     case.
                                 * -E_REND_NO_MSG
                                 *     send_msg has no NO_MSG branch — it's
                                 *     here. it should not happen for it's
                                 *     sender side (if using the correct
                                 *     protocol). but we have also use
                                 *     ipc_port_try_match to get a receiver, we
                                 *     must also empty-wake
                                 * -E_IN_PARAM
                                 *     we have checked before, it should not
                                 *     happen.
                                 */
                                ipc_empty_wake_peer(
                                        receiver_request->thread,
                                        thread_status_block_on_receive);
                                ref_put(&receiver_request->ms_queue_node
                                                 .refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return try_transfer_result;
                        }
                        }
                } else {
                        u64 old_status = thread_set_status(
                                sender, thread_status_block_on_send);
                        error_t enqueue_result = ipc_port_enqueue_wait(
                                port, IPC_PORT_STATE_SEND, sender);
                        switch (enqueue_result) {
                        case REND_SUCCESS: {
                                /*successfully enqueue on the port
                                 * queue, need schedule; when we are
                                 * woken up, a receiver has matched us
                                 * and transferred the message*/
                                /*
                                 * Unlock before schedule so
                                 * unregister can wait count==0 and clean
                                 * while we sleep, we also do it at recv_msg
                                 */
                                port_ops_end(port);
                                /**
                                 * Do not change the following comments:
                                 * For some cases(like futex),
                                 * after we enqueue and before schedule,
                                 * we should add a check, if the check pass,
                                 * it means the condition of block is not
                                 * satisify any more.
                                 * so we should not schedule and blocked.
                                 * Otherwise it have been waken before, so no
                                 * more opposite will wake it any more, and it
                                 * will sleep forever.
                                 * so we add a should_block_hook for every IPC.
                                 * The key point is ,it must enqueue first.
                                 * Then using CAS to change sender's port_ptr.
                                 *
                                 * if a match success befor CAS, the CAS will
                                 * fail, and continue scheule, but no problem,
                                 * it have been changed to ready, so it check
                                 * fail but receive a message and will not sleep
                                 * forever.
                                 *
                                 * if a match success after CAS, it also no
                                 * problem, in ipc_port_try_match also have a
                                 * CAS, it will fail, and find the port is not
                                 * the port(it's NULL), which just put an old
                                 * ipc request into the queue and the opposite
                                 * thread will retry for next one(if exist).
                                 *
                                 * For upper layer, it should check this again
                                 * error code and retry.
                                 *
                                 * Because it's send msg, so it should also drop
                                 * the orphan send msg. But no need for receive.
                                 *
                                 * If you want using lock but don't want sleep
                                 * with lock hold. You should unlock->
                                 * send_msg/recv_msg -> lock, and should
                                 * recheck after we hold the lock. We do not
                                 * support unlock all the lock in core here and
                                 * no need.
                                 */
                                if (sender->should_block_hook
                                    && !sender->should_block_hook(
                                            sender->should_block_ctx)) {
                                        if (atomic64_cas((volatile u64*)&sender
                                                                 ->port_ptr,
                                                         (u64)port,
                                                         (u64)NULL)
                                            == (u64)port) {
                                                ipc_drop_one_orphan_send_msg(
                                                        sender);
                                                /* for the following check fail,
                                                 * current have no case will
                                                 * happen if our model have no
                                                 * problem, we just have to let
                                                 * CAS port first*/
                                                if (thread_set_status_with_expect(
                                                            sender,
                                                            thread_status_block_on_send,
                                                            old_status)) {
                                                        return -E_REND_AGAIN;
                                                }
                                        }
                                }
                                schedule(percpu(core_tm));
                                if (sender->flags
                                    & THREAD_FLAG_IPC_PORT_CLOSED) {
                                        sender->flags = clear_mask_u64(
                                                sender->flags,
                                                THREAD_FLAG_IPC_PORT_CLOSED);
                                        /*
                                         * if we are woken, and find the port is
                                         * closed, but the send msg is still in
                                         * the queue or on the ptr, so clean it
                                         */
                                        ipc_drop_one_orphan_send_msg(sender);
                                        return -E_REND_PORT_CLOSED;
                                }
                                if (sender->flags
                                    & THREAD_FLAG_IPC_EMPTY_WAKE) {
                                        sender->flags = clear_mask_u64(
                                                sender->flags,
                                                THREAD_FLAG_IPC_EMPTY_WAKE);
                                        /* Message still queued; just let caller
                                         * retries. */
                                        return -E_REND_AGAIN;
                                }
                                return REND_SUCCESS;
                        }
                        case -E_REND_AGAIN: {
                                /*the queue have change the state, we
                                 * need retry.*/
                                thread_set_status_with_expect(
                                        sender,
                                        thread_status_block_on_send,
                                        old_status);
                                continue;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                thread_set_status_with_expect(
                                        sender,
                                        thread_status_block_on_send,
                                        old_status);
                                port_ops_end(port);
                                return -E_RENDEZVOS;
                        }
                        }
                }
        }
}
error_t recv_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* receiver = get_cpu_current_thread();
        Ipc_Request_t* sender_request = NULL;
        if (!receiver) {
                return -E_RENDEZVOS;
        }
        if (!port_ops_begin(port)) {
                return -E_REND_PORT_CLOSED;
        }

        while (1) {
                if (!sender_request) {
                        sender_request =
                                ipc_port_try_match(port, IPC_PORT_STATE_RECV);
                }
                if (sender_request) {
                        error_t try_transfer_result = ipc_transfer_message(
                                sender_request->thread, receiver);
                        switch (try_transfer_result) {
                        case REND_SUCCESS: {
                                /*successfully receive a message*/
                                thread_set_status_with_expect(
                                        sender_request->thread,
                                        thread_status_block_on_send,
                                        thread_status_ready);
                                ref_put(&sender_request->ms_queue_node.refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return REND_SUCCESS;
                        }
                        case -E_REND_NO_MSG: {
                                /*
                                 * For -E_REND_NO_MSG, means cannot get a
                                 * message from sender so we need to wake the
                                 * sender and let it handle this.
                                 */
                                ipc_empty_wake_peer(
                                        sender_request->thread,
                                        thread_status_block_on_send);
                                ref_put(&sender_request->ms_queue_node.refcount,
                                        free_ipc_request);
                                sender_request = NULL;
                                continue;
                        }
                        case -E_REND_AGAIN: {
                                /*impossible, only receiver is exiting
                                 * can return -E_REND_AGAIN*/
                                ref_put(&sender_request->ms_queue_node.refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return -E_RENDEZVOS;
                        }
                        default: {
                                /* DO NOT CHANGE THE FOLLOWING COMMENTS:
                                 * Matched sender; empty-wake then return.
                                 * From ipc_transfer_message:
                                 * -E_REND_NO_MEM
                                 *     create_message_structure failed(OOM);
                                 *     untouch send queue/send_pending_ptr (msg
                                 *     still there). so it's standard empty-wake
                                 *     case.
                                 * -E_IN_PARAM
                                 *     sender/receiver NULL (should not here).
                                 * -E_RENDEZVOS
                                 *     seems also impossible here.
                                 */
                                ipc_empty_wake_peer(
                                        sender_request->thread,
                                        thread_status_block_on_send);
                                ref_put(&sender_request->ms_queue_node.refcount,
                                        free_ipc_request);
                                port_ops_end(port);
                                return try_transfer_result;
                        }
                        }
                } else {
                        u64 old_status = thread_set_status(
                                receiver, thread_status_block_on_receive);
                        error_t enqueue_result = ipc_port_enqueue_wait(
                                port, IPC_PORT_STATE_RECV, receiver);
                        switch (enqueue_result) {
                        case REND_SUCCESS: {
                                /*successfully enqueue on the port
                                 * queue; when we are woken up, a sender
                                 * has matched us and transferred the
                                 * message*/
                                port_ops_end(port);
                                /*
                                 * The same with the send msg. But no
                                 * ipc_drop_one_orphan_send_msg here.
                                 */
                                if (receiver->should_block_hook
                                    && !receiver->should_block_hook(
                                            receiver->should_block_ctx)) {
                                        if (atomic64_cas((volatile u64*)&receiver
                                                                 ->port_ptr,
                                                         (u64)port,
                                                         (u64)NULL)
                                            == (u64)port) {
                                                if (thread_set_status_with_expect(
                                                            receiver,
                                                            thread_status_block_on_receive,
                                                            old_status)) {
                                                        return -E_REND_AGAIN;
                                                }
                                        }
                                }
                                schedule(percpu(core_tm));
                                if (receiver->flags
                                    & THREAD_FLAG_IPC_PORT_CLOSED) {
                                        receiver->flags = clear_mask_u64(
                                                receiver->flags,
                                                THREAD_FLAG_IPC_PORT_CLOSED);
                                        return -E_REND_PORT_CLOSED;
                                }
                                if (receiver->flags
                                    & THREAD_FLAG_IPC_EMPTY_WAKE) {
                                        receiver->flags = clear_mask_u64(
                                                receiver->flags,
                                                THREAD_FLAG_IPC_EMPTY_WAKE);
                                        return -E_REND_AGAIN;
                                }
                                return REND_SUCCESS;
                        }
                        case -E_REND_AGAIN: {
                                /*the queue have change the state, we
                                 * need retry.*/
                                thread_set_status_with_expect(
                                        receiver,
                                        thread_status_block_on_receive,
                                        old_status);
                                continue;
                        }
                        default: {
                                /*no other case is set now, and seems
                                 * impossible*/
                                thread_set_status_with_expect(
                                        receiver,
                                        thread_status_block_on_receive,
                                        old_status);
                                port_ops_end(port);
                                return -E_RENDEZVOS;
                        }
                        }
                }
        }
}
error_t ipc_try_send_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* sender = get_cpu_current_thread();
        if (!sender) {
                return -E_REND_AGAIN;
        }
        Ipc_Request_t* receiver_request = NULL;
        if (!port_ops_begin(port)) {
                ipc_drop_one_orphan_send_msg(sender);
                return -E_REND_PORT_CLOSED;
        }

        while (1) {
                if (!receiver_request) {
                        receiver_request =
                                ipc_port_try_match(port, IPC_PORT_STATE_SEND);
                }
                if (!receiver_request) {
                        port_ops_end(port);
                        return -E_REND_AGAIN;
                }
                error_t try_transfer_result =
                        ipc_transfer_message(sender, receiver_request->thread);
                switch (try_transfer_result) {
                case REND_SUCCESS: {
                        thread_set_status_with_expect(
                                receiver_request->thread,
                                thread_status_block_on_receive,
                                thread_status_ready);
                        ref_put(&receiver_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return REND_SUCCESS;
                }
                case -E_REND_AGAIN: {
                        /*
                         * Same as send_msg: receiver exiting. Put this
                         * request and match again (message on pending).
                         * Do not empty-wake.
                         */
                        ref_put(&receiver_request->ms_queue_node.refcount,
                                free_ipc_request);
                        receiver_request = NULL;
                        continue;
                }
                case -E_RENDEZVOS: {
                        /* Same as send_msg: no empty-wake (impossible path). */
                        ref_put(&receiver_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return -E_RENDEZVOS;
                }
                default: {
                        /*
                         * Empty-wake: same as send_msg default
                         * (NO_MEM / NO_MSG / unreachable IN_PARAM).
                         */
                        ipc_empty_wake_peer(receiver_request->thread,
                                            thread_status_block_on_receive);
                        ref_put(&receiver_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return try_transfer_result;
                }
                }
        }
}
error_t ipc_try_recv_msg(Message_Port_t* port)
{
        if (!port) {
                return -E_IN_PARAM;
        }
        Thread_Base* receiver = get_cpu_current_thread();
        if (!receiver) {
                return -E_REND_AGAIN;
        }
        Ipc_Request_t* sender_request = NULL;
        if (!port_ops_begin(port)) {
                return -E_REND_PORT_CLOSED;
        }

        while (1) {
                if (!sender_request) {
                        sender_request =
                                ipc_port_try_match(port, IPC_PORT_STATE_RECV);
                }
                if (!sender_request) {
                        port_ops_end(port);
                        return -E_REND_AGAIN;
                }
                error_t try_transfer_result =
                        ipc_transfer_message(sender_request->thread, receiver);
                switch (try_transfer_result) {
                case REND_SUCCESS: {
                        thread_set_status_with_expect(
                                sender_request->thread,
                                thread_status_block_on_send,
                                thread_status_ready);
                        ref_put(&sender_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return REND_SUCCESS;
                }
                case -E_REND_NO_MSG: {
                        /*
                         * Same as recv_msg NO_MSG
                         */
                        ipc_empty_wake_peer(sender_request->thread,
                                            thread_status_block_on_send);
                        ref_put(&sender_request->ms_queue_node.refcount,
                                free_ipc_request);
                        sender_request = NULL;
                        continue;
                }
                case -E_REND_AGAIN: {
                        ref_put(&sender_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return -E_RENDEZVOS;
                }
                default: {
                        /*
                         * Empty-wake:Same as recv_msg default
                         */
                        ipc_empty_wake_peer(sender_request->thread,
                                            thread_status_block_on_send);
                        ref_put(&sender_request->ms_queue_node.refcount,
                                free_ipc_request);
                        port_ops_end(port);
                        return try_transfer_result;
                }
                }
        }
}

error_t enqueue_msg_for_send(Message_t* msg)
{
        if (!msg)
                return -E_IN_PARAM;
        Thread_Base* self = get_cpu_current_thread();
        if (!self)
                return -E_REND_AGAIN;
        if (!ref_count(&msg->ms_queue_node.refcount)
            || (msg->data && !ref_count(&msg->data->refcount))) {
                return -E_REND_RC_UNEQUAL;
        }
        msq_enqueue(
                &self->send_msg_queue, &msg->ms_queue_node, free_message_ref);
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
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
        atomic64_sub(&self->recv_pending_cnt, 1);
        ms_queue_node_t* msg_node = (ms_queue_node_t*)tp_get_ptr(dp);
        return container_of(msg_node, Message_t, ms_queue_node);
}
/*
DO NOT CHANGE THE FOLLOWING COMMENT!!!
a problem is if a sender or a receiver is not a thread
For example, the timer interrupt try to send a message to a thread which is
waiting on a port.

But, what is the timer interrupt's thread???
The current thread ? No!!!
if the current thread's send msg queue have some msg,
the ipc transfer message func will only try to get a msg from the head of the
send queue, but the new message must enqueue to the end of the queue. It will be
an error.

More and more, if the system itself (maybe some interrupt handler) try to send
or receiver a message to a thread that block on a port. which thread should be
use?

So we temporarily modify the value of current thread, setting it to the idle
thread, and then use the idle thread to send messages. The idle thread acts as a
proxy thread. Afterwards, we change the current thread back.

However, we must send messages sequentially one by one. But this is not the case
in the queue. We must cache only one message at a time, attempt to send it, and
if it fails, clear the message. Therefore, we directly use the sender's
`send_pending_msg` pointer. The key is that it is compatible with the design of
`ipc_transfer_message`. So we should not change any other interfaces.

Besides, if the system need receive, we designed using the boot thread.
For example, the orphan need to let the boot clean it, we design it at
cmain(boot) end（maybe also start secondary cpu). But the boot thread might
receive and then send, and it might also blocked, which is not match the system
send model. ——the system send model need the send queue is clean.
*/

error_t ipc_system_try_deliver(Message_Port_t* port, Message_t* msg,
                               bool use_system_proxy)
{
        Thread_Base* curr = get_cpu_current_thread();
        Thread_Base* proxy = percpu(idle_thread_ptr);
        Thread_Base* sender = use_system_proxy ? proxy : curr;
        error_t err;

        if (!port || !msg)
                return -E_IN_PARAM;
        if (!ref_count(&msg->ms_queue_node.refcount)
            || (msg->data && !ref_count(&msg->data->refcount))) {
                return -E_REND_RC_UNEQUAL;
        }
        if (!sender)
                return -E_REND_AGAIN;
        if (use_system_proxy && !percpu(core_tm))
                return -E_IN_PARAM;

        if (atomic64_cas((volatile u64*)&sender->send_pending_msg,
                         (u64)NULL,
                         (u64)msg)
            != (u64)NULL) {
                /* Slot busy: retryable */
                return -E_REND_AGAIN;
        }

        if (use_system_proxy)
                set_cpu_current_thread(proxy);

        err = ipc_try_send_msg(port);

        if (use_system_proxy)
                set_cpu_current_thread(curr);

        if (err != REND_SUCCESS) {
                Message_t* pending = (Message_t*)atomic64_exchange(
                        (volatile u64*)&sender->send_pending_msg, (u64)NULL);
                if (pending)
                        ref_put(&pending->ms_queue_node.refcount,
                                free_message_ref);
        }
        return err;
}

error_t ipc_system_deliver_to(Thread_Base* receiver, Message_t* msg,
                              bool use_system_proxy)
{
        Thread_Base* sender;
        error_t err;

        if (!receiver || !msg)
                return -E_IN_PARAM;
        if (!ref_count(&msg->ms_queue_node.refcount)
            || (msg->data && !ref_count(&msg->data->refcount))) {
                return -E_REND_RC_UNEQUAL;
        }

        sender = use_system_proxy ? percpu(idle_thread_ptr) :
                                    get_cpu_current_thread();
        if (!sender)
                return -E_REND_AGAIN;

        if (atomic64_cas((volatile u64*)&sender->send_pending_msg,
                         (u64)NULL,
                         (u64)msg)
            != (u64)NULL) {
                /* Same as the ipc_system_try_deliver retry */
                return -E_REND_AGAIN;
        }

        err = ipc_transfer_message(sender, receiver);
        if (err != REND_SUCCESS) {
                Message_t* pending = (Message_t*)atomic64_exchange(
                        (volatile u64*)&sender->send_pending_msg, (u64)NULL);
                if (pending)
                        ref_put(&pending->ms_queue_node.refcount,
                                free_message_ref);
        }
        return err;
}