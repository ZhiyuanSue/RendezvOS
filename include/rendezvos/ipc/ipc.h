#ifndef _RENDEZVOS_IPC_H_
#define _RENDEZVOS_IPC_H_

#include <rendezvos/error.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/mm/allocator.h>
#include <common/string.h>
#include <rendezvos/task/tcb.h>

/*ipc request structure*/
typedef struct {
        ms_queue_node_t ms_queue_node;
        Thread_Base* thread;
        ms_queue_t* queue_ptr;
} Ipc_Request_t;

Ipc_Request_t* create_ipc_request(Thread_Base* thread);
void delete_ipc_request(Ipc_Request_t* req);
error_t free_ipc_request(ref_count_t* refcount);

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
 * @brief Non-blocking send: same preconditions and transfer path as `send_msg`,
 * but returns `-E_REND_AGAIN` instead of blocking on the port when no receiver
 * is waiting.
 *
 * The caller must `enqueue_msg_for_send(msg)` before this call (same as
 * `send_msg`). `ipc_transfer_message` pulls from the current thread's send
 * queue. If this returns `-E_REND_AGAIN`, the message remains enqueued on
 * the send queue unless the caller removes it.
 *
 * On success the matched receiver moves from `thread_status_block_on_receive`
 * to `thread_status_ready`.
 *
 * @param port Port to send on.
 * @return REND_SUCCESS message transferred; `-E_REND_AGAIN` if no receiver is
 *         waiting; other negative errors on failure.
 */
error_t ipc_try_send_msg(Message_Port_t* port);

/**
 * @brief Receive a message from the port. After return REND_SUCCESS, please
 * dequeue the message from current thread's recv queue (e.g. via
 * dequeue_recv_msg).
 * @param port Port to receive from.
 * @return REND_SUCCESS a message was received; negative error on failure.
 */
error_t recv_msg(Message_Port_t* port);

/**
 * @brief Non-blocking receive: try to match a sender on @p port and pull one
 * message without enqueueing the receiver on the port wait queue.
 *
 * On success the matched sender is moved from `thread_status_block_on_send` to
 * `thread_status_ready`; the caller must `dequeue_recv_msg()` to obtain the
 * message (same as `recv_msg`).
 *
 * @param port Port to receive from.
 * @return REND_SUCCESS a message was received; `-E_REND_AGAIN` if no sender is
 *         waiting; other negative errors on failure.
 */
error_t ipc_try_recv_msg(Message_Port_t* port);

/**
 * @brief Enqueue a message to the current thread's send queue (before
 * send_msg).
 * @param msg Message to enqueue; must not be NULL.
 * @return REND_SUCCESS on success; -E_IN_PARAM if msg is NULL;
 *         -E_REND_AGAIN if no current thread (e.g. wrong context).
 */
error_t enqueue_msg_for_send(Message_t* msg);

/**
 * @brief Dequeue one message from the current thread's recv queue (after
 * recv_msg). Hides ms_queue dummy-node semantics: returns the actual Message_t,
 * not the internal head node. Decrements recv_pending_cnt for each message
 * dequeued.
 * @return The Message_t* (caller must ref_dec when done),
 *         or NULL if queue is empty or no current thread.
 */
Message_t* dequeue_recv_msg(void);

/*
 * System IPC (async outbound from IRQ / trap)
 *
 * Two delivery models (see lockfree-ipc.md §9.3):
 *
 * 1. ipc_system_try_deliver(port, msg) — timer / wait_port: receiver is whoever
 *    is block_on_receive on @p port (try_match). Requires a dedicated waiter.
 *
 * 2. ipc_system_deliver_to(receiver, msg) — device IRQ (e.g. NIC): @p receiver
 *    is bound at init (driver kthread). No port rendezvous; message lands in
 *    receiver->recv_msg_queue; receiver drains with dequeue_recv_msg in thread
 *    context (need not be blocked on a port).
 *
 * Both use the per-CPU system proxy (boot: idle_thread_ptr) as sender,
 * send_pending_msg single-slot staging, and exchange clean on failure. Do not
 * use enqueue_msg_for_send on the proxy.
 *
 * Ordinary thread IPC stays current-thread-only (send_msg / recv_msg). We do
 * not add Thread_Base* to public send_msg / ipc_try_send_msg — only these core
 * async entry points take an explicit receiver where the model requires it.
 */

/**
 * @brief Best-effort deliver one message from the system proxy to a waiter
 * on @p port (non-blocking).
 *
 * Port try_match path (mode 1). Stages @p msg in
 * proxy->send_pending_msg, swaps current_thread to proxy, calls
 * ipc_try_send_msg(@p port), try-clean on failure.
 *
 * Caller transfers ownership of @p msg on entry. After staging, failure
 * releases
 * @p msg; success consumes it via ipc_transfer_message.
 *
 * @param port Target wait port (selects the blocked receiver).
 * @param msg Message to deliver.
 * @return REND_SUCCESS transferred; -E_REND_AGAIN if no receiver on port or no
 *         proxy; -E_REND_IPC if slot busy or invalid refs; -E_IN_PARAM if
 *         port or msg is NULL.
 */
error_t ipc_system_try_deliver(Message_Port_t* port, Message_t* msg);

/**
 * @brief Best-effort deliver one message from the system proxy directly to
 * @p receiver (non-blocking).
 *
 * Known-receiver path (mode 2, e.g. NIC IRQ). Stages @p msg in
 * proxy->send_pending_msg and calls ipc_transfer_message(proxy, @p
 * receiver) without port try_match. @p receiver need not be blocked on a port;
 * it should dequeue from its recv queue in thread context.
 *
 * Caller must keep @p receiver valid for the call (typically a driver kthread
 * bound at init). Same staging / try-clean / single in-flight constraints as
 * ipc_system_try_deliver.
 *
 * @param receiver Target thread (recv_msg_queue).
 * @param msg Message to deliver.
 * @return REND_SUCCESS transferred; -E_REND_AGAIN if proxy unavailable or
 *         transfer failed with receiver exiting; -E_REND_IPC if slot busy or
 *         invalid refs; -E_IN_PARAM if receiver or msg is NULL.
 */
error_t ipc_system_deliver_to(Thread_Base* receiver, Message_t* msg);

#endif