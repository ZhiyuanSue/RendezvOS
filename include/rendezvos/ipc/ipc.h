#ifndef _RENDEZVOS_IPC_H_
#define _RENDEZVOS_IPC_H_

#include <rendezvos/error.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/mm/allocator.h>
#include <common/string.h>
#include <rendezvos/task/thread.h>

/**
 * @brief Port wait-queue node: one blocked sender or receiver on a port.
 *
 * Linked via @c ms_queue_node into @c Message_Port_t::thread_queue. Holds a
 * live ref on @c thread until the node is retired. @c queue_ptr may record the
 * owning queue for diagnostics / reclaim paths.
 */
typedef struct {
        ms_queue_node_t ms_queue_node;
        Thread_Base* thread;
        ms_queue_t* queue_ptr;
} Ipc_Request_t;

/**
 * @brief Allocate a port wait-queue node that holds a ref on @p thread.
 * @return New request with refcount 1, or NULL if @p thread is NULL / get fails
 *         / alloc fails.
 */
Ipc_Request_t* create_ipc_request(Thread_Base* thread);

/**
 * @brief Free an IPC request structure (puts @c thread ref if set).
 */
void delete_ipc_request(Ipc_Request_t* req);

/**
 * @brief Refcount destructor for @c Ipc_Request_t; 
 */
error_t free_ipc_request(ref_count_t* refcount);

/**
 * @brief Transfer one message from @p sender to @p receiver.
 *
 * Caller must already have matched the pair and hold refs as required by the
 * send/recv path. Takes a message from the sender (@c send_pending_msg or
 * @c send_msg_queue) and places a new @c Message_t on the receiver that shares
 * the same @c Msg_Data. Does not touch port wait queues.
 *
 * @return @c REND_SUCCESS; @c -E_REND_AGAIN if receiver is exiting (message
 *         kept on sender for retry on the sender-push path);
 *         @c -E_REND_NO_MSG if sender has no message; other negatives on
 *         alloc/param failure.
 */
error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver);

/**
 * @brief Blocking send on @p port (message already on current send queue).
 *
 * Preconditions: @c enqueue_msg_for_send(msg) first. Uses @c port_ops_begin
 * (@c PORT_OPS_SEND); on begin fail drops one orphan send msg and returns
 * @c -E_REND_PORT_CLOSED.
 *
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p port is NULL; @c -E_RENDEZVOS
 *         if no current thread; @c -E_REND_PORT_CLOSED on begin fail or
 *         @c THREAD_FLAG_IPC_PORT_CLOSED wake; other transfer errors from
 *         @c ipc_transfer_message.
 * @note May block (@c block_on_send) until a receiver is matched. Call
 *       @c port_ops_end **before** @c schedule (unregister waits
 *       @c ops_count==0).
 */
error_t send_msg(Message_Port_t* port);

/**
 * @brief Non-blocking send: same enqueue precondition and transfer path as
 *        @c send_msg, but returns @c -E_REND_AGAIN instead of blocking when no
 *        receiver is waiting.
 *
 * Caller must @c enqueue_msg_for_send(msg) first. Transfer pulls from the
 * current thread's send queue / @c send_pending_msg. On @c -E_REND_AGAIN the
 * message remains enqueued unless the caller removes it. On begin fail, drops
 * one orphan send msg (same as @c send_msg).
 *
 * On success the matched receiver moves from @c thread_status_block_on_receive
 * to @c thread_status_ready.
 *
 * @param port Port to send on.
 * @return @c REND_SUCCESS; @c -E_REND_AGAIN if no receiver waiting or no
 *         current thread; @c -E_IN_PARAM if @p port is NULL;
 *         @c -E_REND_PORT_CLOSED on begin fail; other transfer errors from
 *         @c ipc_transfer_message.
 */
error_t ipc_try_send_msg(Message_Port_t* port);

/**
 * @brief Blocking receive on @p port.
 *
 * On @c REND_SUCCESS the caller must @c dequeue_recv_msg() to take the
 * message. May block (@c block_on_receive) until a sender is matched.
 *
 * @return @c REND_SUCCESS (including port-close wake that delivered
 *         @c KMSG_OP_SYSTEM_PORT_CLOSED — detect via opcode after dequeue);
 *         @c -E_IN_PARAM / @c -E_RENDEZVOS; @c -E_REND_PORT_CLOSED if begin
 *         fails or woken with @c THREAD_FLAG_IPC_PORT_CLOSED.
 * @note Call @c port_ops_end before @c schedule (same as send). Close-wake is
 *       **not** symmetric with @c send_msg: senders always see the flag;
 *       receivers preferably get the system closed kmsg (see
 *       @c unregister_port).
 */
error_t recv_msg(Message_Port_t* port);

/**
 * @brief Non-blocking receive: match a sender on @p port and pull one message
 *        without enqueueing this receiver on the port wait queue.
 *
 * On success the matched sender moves from @c thread_status_block_on_send to
 * @c thread_status_ready; the caller must @c dequeue_recv_msg() (same as
 * @c recv_msg).
 *
 * @param port Port to receive from.
 * @return @c REND_SUCCESS; @c -E_REND_AGAIN if no sender waiting or no current
 *         thread; @c -E_IN_PARAM if @p port is NULL; @c -E_REND_PORT_CLOSED on
 *         begin fail; other transfer errors from @c ipc_transfer_message.
 */
error_t ipc_try_recv_msg(Message_Port_t* port);

/**
 * @brief Enqueue a message on the current thread's send queue (before send).
 * @param msg Message to enqueue; must not be NULL; msg (and data if set) must
 *        have live refcount.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p msg is NULL; @c -E_REND_AGAIN
 *         if no current thread; @c -E_REND_IPC if refcounts invalid.
 * @note After enqueue the queue owns a ref (@c ref_put on the caller's shell
 *       ref). Do not use for @c ipc_system_* deliver (those stage via
 *       @c send_pending_msg instead).
 */
error_t enqueue_msg_for_send(Message_t* msg);

/**
 * @brief Dequeue one message from the current thread's recv queue (after recv).
 *
 * Decrements @c recv_pending_cnt once per success.
 *
 * @return Message with ownership for the caller (@c ref_put via
 *         @c free_message_ref when done), or NULL if empty / no current thread.
 */
Message_t* dequeue_recv_msg(void);

/*
 * System IPC (async outbound when the sender may not be a normal thread)
 *
 * Two delivery models:
 *
 * 1. ipc_system_try_deliver(port, msg, use_system_proxy) — deliver to a waiter
 *    blocked on @p port (non-blocking try).
 *
 * 2. ipc_system_deliver_to(receiver, msg, use_system_proxy) — deliver directly
 *    to a known @p receiver. No port rendezvous; receiver drains with
 *    @c dequeue_recv_msg in thread context.
 *
 * Both stage one outbound message in the sender's @c send_pending_msg (same
 * path as @c ipc_transfer_message). Do not use @c enqueue_msg_for_send here.
 *
 * @c use_system_proxy selects the staging sender: the per-CPU idle persona
 * (true) or the current thread (false).
 *
 * Ordinary thread IPC stays current-thread-only (@c send_msg / @c recv_msg).
 */

/**
 * @brief Best-effort deliver one message to a waiter on @p port (non-blocking).
 *
 * Stages @p msg in the sender's @c send_pending_msg, then tries a non-blocking
 * send on @p port. When @p use_system_proxy is true, stages as the per-CPU idle
 * persona; when false, stages on the current thread.
 *
 * Ownership: pre-stage failures leave @p msg with the caller. After a
 * successful stage, success consumes it; post-stage failure cleans the slot
 * and releases it.
 *
 * @param port Target wait port (selects the blocked receiver).
 * @param msg Message to deliver.
 * @param use_system_proxy True → stage as the per-CPU idle persona; false →
 *        stage on the current thread.
 * @return @c REND_SUCCESS; @c -E_REND_AGAIN (no waiter / no sender persona);
 *         @c -E_REND_PORT_CLOSED (begin deny/close); @c -E_REND_IPC (slot busy /
 *         bad refs); @c -E_IN_PARAM (@p port/@p msg NULL, or proxy without
 *         @c core_tm); other transfer errors.
 */
error_t ipc_system_try_deliver(Message_Port_t* port, Message_t* msg,
                               bool use_system_proxy);

/**
 * @brief Best-effort deliver one message directly to @p receiver
 *        (non-blocking).
 *
 * Stages @p msg and transfers to @p receiver without port matching. @p receiver
 * need not be blocked on a port; it should dequeue from its recv queue in
 * thread context.
 *
 * @p use_system_proxy selects idle persona vs current thread as sender (same
 * staging / ownership rules as @c ipc_system_try_deliver).
 *
 * Caller must keep @p receiver valid for the duration of the call.
 *
 * @param receiver Target thread (@c recv_msg_queue).
 * @param msg Message to deliver.
 * @param use_system_proxy True → idle persona; false → current thread.
 * @return @c REND_SUCCESS; @c -E_REND_AGAIN if receiver exiting or no sender
 *         persona; @c -E_REND_NO_MSG; @c -E_REND_IPC if slot busy / bad refs;
 *         @c -E_IN_PARAM if @p receiver or @p msg is NULL. Same ownership rule
 *         as @c ipc_system_try_deliver (pre-stage keep; post-stage clean/release).
 */
error_t ipc_system_deliver_to(Thread_Base* receiver, Message_t* msg,
                              bool use_system_proxy);

#endif