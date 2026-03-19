/*
 * Port discovery test: receiver thread registers a port by name; sender
 * thread looks up the port by name and sends a message. Verifies register,
 * lookup, IPC, and unregister.
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/message.h>
#include <rendezvos/task/port.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/smp/percpu.h>
#include <common/stddef.h>
#include <common/string.h>

#define PORT_DISCOVERY_PORT_NAME "svc"
#define PORT_DISCOVERY_MSG_TYPE  100
static char port_discovery_payload[32]
        __attribute__((aligned(16))) = "port_discovery_hello\0";

static volatile int port_discovery_receiver_done;
static volatile int port_discovery_sender_done;
static volatile i64 port_discovery_recv_type;
static char port_discovery_recv_buf[64] __attribute__((aligned(16)));

static Message_Port_t* receiver_port = NULL;

static void* port_discovery_receiver_thread(void* arg)
{
        (void)arg;
        receiver_port = create_message_port(PORT_DISCOVERY_PORT_NAME);
        if (!receiver_port) {
                pr_error("[port_test] receiver: create port failed\n");
                port_discovery_receiver_done = 1;
                return NULL;
        }
        if (register_port(global_port_table, receiver_port) != REND_SUCCESS) {
                pr_error("[port_test] receiver: register failed\n");
                delete_message_port_structure(receiver_port);
                receiver_port = NULL;
                port_discovery_receiver_done = 1;
                return NULL;
        }
        if (recv_msg(receiver_port) != REND_SUCCESS) {
                pr_error("[port_test] receiver: recv_msg failed\n");
                unregister_port(global_port_table, PORT_DISCOVERY_PORT_NAME);
                delete_message_port_structure(receiver_port);
                receiver_port = NULL;
                port_discovery_receiver_done = 1;
                return NULL;
        }
        Message_t* msg = dequeue_recv_msg();
        if (!msg || !msg->data) {
                pr_error("[port_test] receiver: no message\n");
                if (msg)
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                unregister_port(global_port_table, PORT_DISCOVERY_PORT_NAME);
                delete_message_port_structure(receiver_port);
                receiver_port = NULL;
                port_discovery_receiver_done = 1;
                return NULL;
        }
        port_discovery_recv_type = msg->data->msg_type;
        u64 len = msg->data->data_len;
        if (len > sizeof(port_discovery_recv_buf) - 1)
                len = sizeof(port_discovery_recv_buf) - 1;
        if (msg->data->data && len) {
                strncpy(port_discovery_recv_buf,
                        (const char*)msg->data->data,
                        len);
                port_discovery_recv_buf[len] = '\0';
        } else {
                port_discovery_recv_buf[0] = '\0';
        }
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
        if (unregister_port(global_port_table, PORT_DISCOVERY_PORT_NAME)
            != REND_SUCCESS)
                pr_error("[port_test] receiver: unregister failed\n");
        delete_message_port_structure(receiver_port);
        receiver_port = NULL;
        port_discovery_receiver_done = 1;
        return NULL;
}

static void* port_discovery_sender_thread(void* arg)
{
        (void)arg;
        Message_Port_t* port = thread_lookup_port(PORT_DISCOVERY_PORT_NAME);
        if (!port) {
                pr_error("[port_test] sender: lookup failed\n");
                port_discovery_sender_done = 1;
                return NULL;
        }
        size_t payload_buf_size = sizeof(port_discovery_payload);
        char* payload = (char*)percpu(kallocator)
                                ->m_alloc(percpu(kallocator), payload_buf_size);
        if (!payload) {
                pr_error("[port_test] sender: alloc payload failed\n");
                port_discovery_sender_done = 1;
                return NULL;
        }
        strncpy(payload, port_discovery_payload, payload_buf_size);
        u64 payload_len = strlen(payload) + 1;
        void* payload_ptr = payload;
        Msg_Data_t* msgdata = create_message_data(PORT_DISCOVERY_MSG_TYPE,
                                                  payload_len,
                                                  &payload_ptr,
                                                  free_msgdata_ref_default);
        if (!msgdata) {
                percpu(kallocator)->m_free(percpu(kallocator), payload);
                port_discovery_sender_done = 1;
                return NULL;
        }
        Message_t* msg = create_message_with_msg(msgdata);
        ref_put(&msgdata->refcount, free_msgdata_ref_default);
        if (!msg) {
                port_discovery_sender_done = 1;
                return NULL;
        }
        if (enqueue_msg_for_send(msg) != REND_SUCCESS) {
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                port_discovery_sender_done = 1;
                return NULL;
        }
        msg = NULL;
        if (send_msg(port) != REND_SUCCESS) {
                pr_error("[port_test] sender: send_msg failed\n");
                ref_put(&port->refcount, free_message_port_ref);
                port_discovery_sender_done = 1;
                return NULL;
        }
        /* 释放lookup时持有的ref */
        ref_put(&port->refcount, free_message_port_ref);
        port_discovery_sender_done = 1;
        return NULL;
}

int single_port_test(void)
{
        Task_Manager* tm = percpu(core_tm);
        error_t e;

        port_discovery_receiver_done = 0;
        port_discovery_sender_done = 0;
        port_discovery_recv_type = -1;
        memset(port_discovery_recv_buf, 0, sizeof(port_discovery_recv_buf));

        is_print_sche_info = false;

        e = gen_thread_from_func(NULL,
                                 port_discovery_receiver_thread,
                                 "port_disc_rcv",
                                 tm,
                                 NULL);
        if (e) {
                pr_error("[port_test] create receiver failed\n");
                return -E_REND_TEST;
        }
        e = gen_thread_from_func(
                NULL, port_discovery_sender_thread, "port_disc_snd", tm, NULL);
        if (e) {
                pr_error("[port_test] create sender failed\n");
                return -E_REND_TEST;
        }

        while (!port_discovery_receiver_done || !port_discovery_sender_done)
                schedule(tm);

        if (port_discovery_recv_type != PORT_DISCOVERY_MSG_TYPE) {
                pr_error("[port_test] recv type %d expected %d\n",
                         (int)port_discovery_recv_type,
                         PORT_DISCOVERY_MSG_TYPE);
                is_print_sche_info = true;
                return -E_REND_TEST;
        }
        if (strcmp(port_discovery_recv_buf, port_discovery_payload) != 0) {
                pr_error("[port_test] recv payload \"%s\" expected \"%s\"\n",
                         port_discovery_recv_buf,
                         port_discovery_payload);
                is_print_sche_info = true;
                return -E_REND_TEST;
        }

        if (thread_lookup_port(PORT_DISCOVERY_PORT_NAME) != NULL) {
                pr_error(
                        "[port_test] lookup after unregister should be NULL\n");
                is_print_sche_info = true;
                return -E_REND_TEST;
        }

        is_print_sche_info = true;
        return REND_SUCCESS;
}
