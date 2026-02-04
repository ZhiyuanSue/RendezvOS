/*
 * Single-core IPC test: one sender thread and one receiver thread
 * communicate through a shared Message_Port_t. Verifies that a message
 * created and sent by the sender is correctly received by the receiver.
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/message.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/smp/percpu.h>
#include <common/stddef.h>
#include <common/string.h>

#define IPC_TEST_MSG_TYPE  42
#define IPC_TEST_PAYLOAD   "ipc_hello"

static volatile int single_ipc_sender_done;
static volatile int single_ipc_receiver_done;
static i64 single_ipc_received_type;
static char single_ipc_received_payload[32];

static void* ipc_sender_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        char payload[] = IPC_TEST_PAYLOAD;
        Message_t* msg = create_message(IPC_TEST_MSG_TYPE,
                                        sizeof(payload),
                                        payload);
        if (!msg) {
                pr_error("[single_ipc_test] sender: create_message failed\n");
                single_ipc_sender_done = 1;
                return NULL;
        }
        if (enqueue_msg_for_send(msg) != REND_SUCCESS) {
                pr_error("[single_ipc_test] sender: enqueue_msg_for_send failed\n");
                message_structure_ref_dec(msg);
                single_ipc_sender_done = 1;
                return NULL;
        }
        if (send_msg(port) != REND_SUCCESS) {
                pr_error("[single_ipc_test] sender: send_msg failed\n");
                message_structure_ref_dec(msg);
                single_ipc_sender_done = 1;
                return NULL;
        }
        single_ipc_sender_done = 1;
        return NULL;
}

static void* ipc_receiver_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        if (recv_msg(port) != REND_SUCCESS) {
                pr_error("[single_ipc_test] receiver: recv_msg failed\n");
                single_ipc_receiver_done = 1;
                return NULL;
        }
        Message_t* msg = dequeue_recv_msg();
        if (!msg) {
                pr_error("[single_ipc_test] receiver: recv_queue empty\n");
                single_ipc_receiver_done = 1;
                return NULL;
        }
        single_ipc_received_type = msg->msg_type;
        if (msg->append_info_len < sizeof(single_ipc_received_payload)) {
                memcpy(single_ipc_received_payload,
                       msg->append_info,
                       msg->append_info_len);
                single_ipc_received_payload[msg->append_info_len] = '\0';
        }
        pr_info("[single_ipc_test] receiver got msg_type=%d payload=\"%s\"\n",
                (int)msg->msg_type, single_ipc_received_payload);
        message_structure_ref_dec(msg);
        single_ipc_receiver_done = 1;
        return NULL;
}

int ipc_test(void)
{
        Message_Port_t* port;
        Task_Manager* tm = percpu(core_tm);
        error_t e;

        single_ipc_sender_done = 0;
        single_ipc_receiver_done = 0;
        single_ipc_received_type = -1;
        memset(single_ipc_received_payload, 0, sizeof(single_ipc_received_payload));

        port = create_message_port();
        if (!port) {
                pr_error("[single_ipc_test] create_message_port failed\n");
                return -E_REND_TEST;
        }

        e = gen_thread_from_func(NULL, ipc_sender_thread, "ipc_sender", tm, port);
        if (e) {
                delete_message_port(port);
                return -E_REND_TEST;
        }
        e = gen_thread_from_func(NULL, ipc_receiver_thread, "ipc_receiver", tm, port);
        if (e) {
                delete_message_port(port);
                return -E_REND_TEST;
        }

        while (!single_ipc_sender_done || !single_ipc_receiver_done)
                schedule(tm);

        delete_message_port(port);

        if (single_ipc_received_type != IPC_TEST_MSG_TYPE) {
                pr_error("[single_ipc_test] received msg_type %d, expected %d\n",
                         (int)single_ipc_received_type, (int)IPC_TEST_MSG_TYPE);
                return -E_REND_TEST;
        }
        if (strcmp(single_ipc_received_payload, IPC_TEST_PAYLOAD) != 0) {
                pr_error("[single_ipc_test] received payload \"%s\", expected \"%s\"\n",
                         single_ipc_received_payload, IPC_TEST_PAYLOAD);
                return -E_REND_TEST;
        }
        return REND_SUCCESS;
}
