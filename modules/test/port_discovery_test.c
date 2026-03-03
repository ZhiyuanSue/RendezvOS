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
static const char port_discovery_payload[] = "port_discovery_hello";

static volatile int port_discovery_receiver_done;
static volatile int port_discovery_sender_done;
static volatile i64 port_discovery_recv_type;
static char port_discovery_recv_buf[64];

static void* port_discovery_receiver_thread(void* arg)
{
        (void)arg;
        Thread_Base* self = get_cpu_current_thread();
        if (!self) {
                pr_error("[port_discovery_test] receiver: no current thread\n");
                port_discovery_receiver_done = 1;
                return NULL;
        }
        if (thread_register_port(self, PORT_DISCOVERY_PORT_NAME)
            != REND_SUCCESS) {
                pr_error("[port_discovery_test] receiver: register failed\n");
                port_discovery_receiver_done = 1;
                return NULL;
        }
        if (recv_msg(self->exposed_port) != REND_SUCCESS) {
                pr_error("[port_discovery_test] receiver: recv_msg failed\n");
                thread_unregister_port(self);
                port_discovery_receiver_done = 1;
                return NULL;
        }
        Message_t* msg = dequeue_recv_msg();
        if (!msg || !msg->data) {
                pr_error("[port_discovery_test] receiver: no message\n");
                if (msg)
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                thread_unregister_port(self);
                port_discovery_receiver_done = 1;
                return NULL;
        }
        port_discovery_recv_type = msg->data->msg_type;
        u64 len = msg->data->data_len;
        if (len > sizeof(port_discovery_recv_buf) - 1)
                len = sizeof(port_discovery_recv_buf) - 1;
        if (msg->data->data && len)
                memcpy(port_discovery_recv_buf, msg->data->data, len);
        port_discovery_recv_buf[len] = '\0';
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
        if (thread_unregister_port(self) != REND_SUCCESS)
                pr_error("[port_discovery_test] receiver: unregister failed\n");
        port_discovery_receiver_done = 1;
        return NULL;
}

static void* port_discovery_sender_thread(void* arg)
{
        (void)arg;
        Message_Port_t* port = thread_lookup_port(PORT_DISCOVERY_PORT_NAME);
        if (!port) {
                pr_error("[port_discovery_test] sender: lookup failed\n");
                port_discovery_sender_done = 1;
                return NULL;
        }
        char* payload = (char*)percpu(kallocator)
                                ->m_alloc(percpu(kallocator),
                                          sizeof(port_discovery_payload));
        if (!payload) {
                pr_error("[port_discovery_test] sender: alloc payload failed\n");
                port_discovery_sender_done = 1;
                return NULL;
        }
        memcpy(payload, port_discovery_payload, sizeof(port_discovery_payload));
        void* payload_ptr = payload;
        Msg_Data_t* msgdata = create_message_data(
                PORT_DISCOVERY_MSG_TYPE,
                (u64)sizeof(port_discovery_payload),
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
        if (send_msg(port) != REND_SUCCESS) {
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                pr_error("[port_discovery_test] sender: send_msg failed\n");
                port_discovery_sender_done = 1;
                return NULL;
        }
        port_discovery_sender_done = 1;
        return NULL;
}

int port_discovery_test(void)
{
        Task_Manager* tm = percpu(core_tm);
        error_t e;

        port_discovery_receiver_done = 0;
        port_discovery_sender_done = 0;
        port_discovery_recv_type = -1;
        memset(port_discovery_recv_buf, 0, sizeof(port_discovery_recv_buf));

        port_discovery_init();

        is_print_sche_info = false;

        e = gen_thread_from_func(NULL,
                                port_discovery_receiver_thread,
                                "port_disc_rcv",
                                tm,
                                NULL);
        if (e) {
                pr_error("[port_discovery_test] create receiver failed\n");
                return -E_REND_TEST;
        }
        e = gen_thread_from_func(NULL,
                                port_discovery_sender_thread,
                                "port_disc_snd",
                                tm,
                                NULL);
        if (e) {
                pr_error("[port_discovery_test] create sender failed\n");
                return -E_REND_TEST;
        }

        while (!port_discovery_receiver_done || !port_discovery_sender_done)
                schedule(tm);

        if (port_discovery_recv_type != PORT_DISCOVERY_MSG_TYPE) {
                pr_error("[port_discovery_test] recv type %d expected %d\n",
                         (int)port_discovery_recv_type,
                         PORT_DISCOVERY_MSG_TYPE);
                is_print_sche_info = true;
                return -E_REND_TEST;
        }
        if (strcmp(port_discovery_recv_buf, port_discovery_payload) != 0) {
                pr_error("[port_discovery_test] recv payload \"%s\" expected \"%s\"\n",
                         port_discovery_recv_buf,
                         port_discovery_payload);
                is_print_sche_info = true;
                return -E_REND_TEST;
        }

        if (thread_lookup_port(PORT_DISCOVERY_PORT_NAME) != NULL) {
                pr_error("[port_discovery_test] lookup after unregister should be NULL\n");
                is_print_sche_info = true;
                return -E_REND_TEST;
        }

        is_print_sche_info = true;
        return REND_SUCCESS;
}
