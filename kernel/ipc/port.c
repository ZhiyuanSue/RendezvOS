#include <rendezvos/ipc/port.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/ipc/kmsg_system.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <common/atomic.h>
#include <common/string.h>
#include <modules/log/log.h>

error_t message_port_init(Message_Port_t* port, u32 service_id)
{
        if (!port)
                return -E_IN_PARAM;

        msq_init(&port->thread_queue, &port->msq_dummy, IPC_PORT_APPEND_BITS);
        ref_init(&port->refcount); /* creator holds one ref */
        port->service_id = service_id;
        port_ops_life_init(port);
        port_ops_count_init(port);
        return REND_SUCCESS;
}

Message_Port_t* create_message_port(u32 service_id)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_Port_t* mp = (Message_Port_t*)cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Message_Port_t));
        if (!mp)
                return NULL;

        message_port_init(mp, service_id);
        return mp;
}

/*
 * It's one-shot OPEN→CLOSING→CLOSED
 * and in a port's life we only have one closing
 * So we can use this try-redo begin function and needn't complex lock
 */
bool port_ops_begin(Message_Port_t* port)
{
        if (!port)
                return false;
        if (!port_is_open(port))
                return false;

        port_ops_count_inc(port);
        if (!port_is_open(port)) {
                port_ops_count_dec(port);
                return false;
        }
        return true;
}

void port_ops_end(Message_Port_t* port)
{
        if (!port)
                return;
        port_ops_count_dec(port);
}

/*
 * clean port->thread_queue on close (also a defensive on final free).
 *
 * send/recv notification differs:
 * the two waiters resume into different API shapes after schedule.
 *
 *   block_on_send  — wakes inside send_msg. That path never dequeues the
 *     thread’s recv queue. So when abort it must drop the orphan send msg
 *     on the send queue and return -E_REND_PORT_CLOSED.
 *     So we just set thread's THREAD_FLAG_IPC_PORT_CLOSED flag.
 *
 *   block_on_receive — wakes inside recv_msg. Success means “dequeue a
 *     message” and server loops are opcode-driven. So we try to send a
 *     KMSG_OP_SYSTEM_PORT_CLOSED kmsg via ipc_system_deliver_to.
 *     Only if recv_pending_cnt still 0 after the attempt,
 *     which means this deliver failed, we set the thread's
 *     THREAD_FLAG_IPC_PORT_CLOSED flag. So the waiter cannot hang
 *     on SUCCESS with an empty queue. Or a successful deliver with a error
 *     return with THREAD_FLAG_IPC_PORT_CLOSED flag set(and leave a kmsg in
 * queue).
 *
 * This function should be aligned with the ipc part and other port part.
 */
static void port_clean_thread_queue(Message_Port_t* port)
{
        tagged_ptr_t dequeued_ptr;

        if (!port) {
                return;
        }

        while (!tp_is_none(dequeued_ptr =
                                   msq_dequeue(&port->thread_queue, NULL))) {
                ms_queue_node_t* node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                Ipc_Request_t* req =
                        container_of(node, Ipc_Request_t, ms_queue_node);
                Thread_Base* thread = req->thread;

                if (thread) {
                        u64 status = thread_get_status(thread);

                        if (status == thread_status_block_on_receive) {
                                Msg_Data_t* msg_data;
                                Message_t* msg;
                                error_t err;

                                atomic64_cas((volatile u64*)&thread->port_ptr,
                                             (u64)port,
                                             (u64)NULL);

                                msg_data =
                                        kmsg_create(port->service_id,
                                                    KMSG_OP_SYSTEM_PORT_CLOSED,
                                                    KMSG_FMT_SYSTEM_PORT_CLOSED,
                                                    (i64)0);
                                if (msg_data) {
                                        msg = create_message_with_msg(msg_data);
                                        ref_put(&msg_data->refcount,
                                                free_msgdata_ref_default);
                                        if (msg) {
                                                err = ipc_system_deliver_to(
                                                        thread, msg, false);
                                                if (err != REND_SUCCESS) {
                                                        ref_put(&msg->ms_queue_node
                                                                         .refcount,
                                                                free_message_ref);
                                                }
                                        }
                                }

                                (void)thread_set_status_with_expect(
                                        thread,
                                        thread_status_block_on_receive,
                                        thread_status_ready);
                                if (atomic64_load((volatile u64*)&thread
                                                          ->recv_pending_cnt)
                                    == 0) {
                                        thread_or_flags(
                                                thread,
                                                THREAD_FLAG_IPC_PORT_CLOSED);
                                }
                        } else if (status == thread_status_block_on_send) {
                                atomic64_cas((volatile u64*)&thread->port_ptr,
                                             (u64)port,
                                             (u64)NULL);
                                thread_or_flags(thread,
                                                THREAD_FLAG_IPC_PORT_CLOSED);
                                (void)thread_set_status_with_expect(
                                        thread,
                                        thread_status_block_on_send,
                                        thread_status_ready);
                        }
                }
                ref_put(&node->refcount, free_ipc_request);
        }
}

void delete_message_port_structure(Message_Port_t* port)
{
        if (!port)
                return;
        /*
         *         port_clean_thread_queue is just a defensive clean here.
         */
        port_clean_thread_queue(port);
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator && !cpu_kallocator->m_free) {
                pr_error(
                        "[port] delete_message_port_structure: m_free is NULL cpu=%lx port_hi=%lx port_lo=%lx\n",
                        (u32)percpu(cpu_number),
                        (u32)(((u64)(uintptr_t)port >> 32) & 0xffffffff),
                        (u32)((u64)(uintptr_t)port & 0xffffffff));
        }
        if (cpu_kallocator)
                cpu_kallocator->m_free(cpu_kallocator, port);
}

static void free_message_port_ref_real(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Message_Port_t* port =
                container_of(ref_count_ptr, Message_Port_t, refcount);

        if (port_is_open(port)) {
                pr_error("[port] free_ref still open");
        }

        delete_message_port_structure(port);
}

error_t free_message_port_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return -E_IN_PARAM;
        free_message_port_ref_real(ref_count_ptr);
        return REND_SUCCESS;
}

error_t close_port(Message_Port_t* port)
{
        if (!port)
                return -E_IN_PARAM;
        if (!port_ops_set_life_with_expect(
                    port, PORT_OPS_LIFE_OPEN, PORT_OPS_LIFE_CLOSING)) {
                pr_error("[port] close life CAS OPEN→CLOSING failed\n");
                return -E_REND_PORT_CLOSED;
        }
        /*
         * It's waiting for the old have beginned send/recv end.
         * and the new sender/receiver's ops_begin will fail, for the status is
         * not OPEN any more.
         */
        while (port_ops_count_get(port) != 0) {
                schedule(percpu(core_tm));
        }
        /* DO NOT CHANGE THE FOLLOWING COMMENTS!
         * The port_clean_thread_queue must be put here.
         * if this clean put at the ref count == 0 and clean time.
         * A case will happen:
         * the port is closed but still some ref,
         * and there's some thread still waiting for the message.
         * but some thread have still hold the ref count,
         * and all the thread are blocked.
         */
        port_clean_thread_queue(port);
        if (!port_ops_set_life_with_expect(
                    port, PORT_OPS_LIFE_CLOSING, PORT_OPS_LIFE_CLOSED)) {
                pr_error("[port] close life CAS CLOSING→CLOSED failed\n");
                return -E_RENDEZVOS;
        }

        /* Does not ref_put — caller drops its own refs after close. */
        return REND_SUCCESS;
}
