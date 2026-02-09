#ifndef _RENDEZVOS_IPC_H_
#define _RENDEZVOS_IPC_H_

#include "../error.h"
#include "message.h"
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

#include "tcb.h"

#define IPC_ENDPOINT_APPEND_BITS 2
#define IPC_ENDPOINT_STATE_EMPTY 0
#define IPC_ENDPOINT_STATE_SEND  1
#define IPC_ENDPOINT_STATE_RECV  2

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t thread_queue;
        /*
        we must record the tcb_queue's dummy_node_ptr,and when we try to free
        the port,we also free this ptr,because other node in this queue,is
        inside the tcb base structure, and they are not always delete after they
        dequeue, but if the dummy is not record, we must try to free the dummy
        node, otherwise this space will lead to a memory leak.
        but this step is not the same when we handle the message, because the
        message always free directly.
        */
        Thread_Base* thread_queue_dummy_node_ptr;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_ENDPOINT_APPEND_BITS) - 1);
}

struct Msg_Port* create_message_port();
void delete_message_port(Message_Port_t* port);

/**
 * @brief Transfer one message from sender to receiver (caller must hold ref to
 * the opposite thread). Gets a message from sender (send_pending_msg or
 * send_msg_queue) and enqueues it to receiver's recv_msg_queue.
 * @param sender Sender thread.
 * @param receiver Receiver thread.
 * @return REND_SUCCESS message transferred; -E_REND_NO_MSG sender has no
 * message (receiver may retry); -E_REND_AGAIN receiver exiting, message
 * returned to sender and caller should retry.
 */
error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver);

/**
 * @brief Send a message on the port. Message must be enqueued to current
 * thread's send queue (e.g. via enqueue_msg_for_send) before calling. Blocks if
 * no receiver.
 * @param port Port to send on.
 * @return REND_SUCCESS a message was sent; negative error on failure.
 */
error_t send_msg(Message_Port_t* port);

/**
 * @brief Receive a message from the port. After return REND_SUCCESS, please
 * dequeue the message from current thread's recv queue (e.g. via
 * dequeue_recv_msg).
 * @param port Port to receive from.
 * @return REND_SUCCESS a message was received; negative error on failure.
 */
error_t recv_msg(Message_Port_t* port);

/**
 * @brief Cancel IPC of a thread blocked on send or recv. Async: after return,
 * check target_thread's status until it is no longer thread_status_cancel_ipc.
 * @param target_thread Thread to cancel IPC for.
 * @return REND_SUCCESS thread dequeued or status set to cancel_ipc; -E_IN_PARAM
 *         if target_thread is NULL; -E_RENDEZVOS on internal error.
 */
error_t cancel_ipc(Thread_Base* target_thread);

/**
 * @brief Enqueue a message to the current thread's send queue (before
 * send_msg).
 * @param msg Message to enqueue; must not be NULL.
 * @return REND_SUCCESS on success; -E_IN_PARAM if msg is NULL;
 *         -E_REND_AGAIN if no current thread (e.g. wrong context).
 */
error_t enqueue_msg_for_send(Message_t* msg, bool refcount_is_zero);

/**
 * @brief Dequeue one message from the current thread's recv queue (after
 * recv_msg). Hides ms_queue dummy-node semantics: returns the actual Message_t,
 * not the internal head node.
 * @return The Message_t* (caller must ref_dec when done),
 *         or NULL if queue is empty or no current thread.
 */
Message_t* dequeue_recv_msg(void);

#endif