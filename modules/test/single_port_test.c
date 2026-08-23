/*
 * Port discovery test: receiver thread registers a port by name; sender
 * thread looks up the port by name and sends a message. Verifies register,
 * lookup, IPC, and unregister.
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/smp/percpu.h>
#include <common/stddef.h>
#include <rendezvos/error.h>
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
        receiver_port = create_message_port(PORT_DISCOVERY_PORT_NAME, NULL);
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

#define PORT_HOOK_DENY_LOOKUP   "hook_deny_lookup"
#define PORT_HOOK_DENY_SEND     "hook_deny_send"
#define PORT_HOOK_DENY_RECV     "hook_deny_recv"
#define PORT_HOOK_DENY_REGISTER "hook_deny_register"
#define PORT_HOOK_TOKEN_LOOKUP  "hook_token_lookup"

/* Toggle at runtime to exercise thread port-cache resolve + lookup gate. */
static volatile int port_hook_token_lookup_deny;

static error_t port_hook_ops_allow(Message_Port_t* port,
                                   enum port_ops_type op_type,
                                   const char* lookup_name)
{
        if (op_type == PORT_OPS_LOOKUP) {
                if (!lookup_name)
                        return REND_SUCCESS;
                if (strcmp_s(lookup_name,
                             PORT_HOOK_DENY_LOOKUP,
                             PORT_NAME_LEN_MAX)
                    == 0)
                        return -E_RENDEZVOS;
                if (strcmp_s(lookup_name,
                             PORT_HOOK_TOKEN_LOOKUP,
                             PORT_NAME_LEN_MAX)
                    == 0
                    && port_hook_token_lookup_deny)
                        return -E_RENDEZVOS;
                return REND_SUCCESS;
        }
        if (!port)
                return REND_SUCCESS;
        if (op_type == PORT_OPS_SEND
            && strcmp_s(port->name,
                        PORT_HOOK_DENY_SEND,
                        PORT_NAME_LEN_MAX)
                       == 0)
                return -E_RENDEZVOS;
        if (op_type == PORT_OPS_RECV
            && strcmp_s(port->name,
                        PORT_HOOK_DENY_RECV,
                        PORT_NAME_LEN_MAX)
                       == 0)
                return -E_RENDEZVOS;
        if (op_type == PORT_OPS_REGISTER
            && strcmp_s(port->name,
                        PORT_HOOK_DENY_REGISTER,
                        PORT_NAME_LEN_MAX)
                       == 0)
                return -E_RENDEZVOS;
        return REND_SUCCESS;
}

static const port_append_hooks_t port_hook_test_hooks = {
        .ops_allow = port_hook_ops_allow,
};

static int port_hook_gate_self_test(void)
{
        Message_Port_t* port;
        error_t e;

        if (!global_port_table)
                return -E_REND_TEST;

        port = create_message_port(PORT_HOOK_DENY_LOOKUP, &port_hook_test_hooks);
        if (!port) {
                pr_error("[port_hook_test] create deny_lookup port failed\n");
                return -E_REND_TEST;
        }
        e = register_port(global_port_table, port);
        if (e != REND_SUCCESS) {
                pr_error("[port_hook_test] register deny_lookup failed e=%d\n",
                         (int)e);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        ref_put(&port->refcount, free_message_port_ref);

        if (port_table_lookup(global_port_table, PORT_HOOK_DENY_LOOKUP)
            != NULL) {
                pr_error("[port_hook_test] deny_lookup port_table_lookup should fail\n");
                unregister_port(global_port_table, PORT_HOOK_DENY_LOOKUP);
                return -E_REND_TEST;
        }
        if (thread_lookup_port(PORT_HOOK_DENY_LOOKUP) != NULL) {
                pr_error("[port_hook_test] deny_lookup thread_lookup should fail\n");
                unregister_port(global_port_table, PORT_HOOK_DENY_LOOKUP);
                return -E_REND_TEST;
        }
        unregister_port(global_port_table, PORT_HOOK_DENY_LOOKUP);

        port_hook_token_lookup_deny = 0;
        port = create_message_port(PORT_HOOK_TOKEN_LOOKUP, &port_hook_test_hooks);
        if (!port) {
                pr_error("[port_hook_test] create token_lookup port failed\n");
                return -E_REND_TEST;
        }
        e = register_port(global_port_table, port);
        if (e != REND_SUCCESS) {
                pr_error("[port_hook_test] register token_lookup failed e=%d\n",
                         (int)e);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        ref_put(&port->refcount, free_message_port_ref);

        port = thread_lookup_port(PORT_HOOK_TOKEN_LOOKUP);
        if (!port) {
                pr_error("[port_hook_test] token_lookup warm cache should succeed\n");
                unregister_port(global_port_table, PORT_HOOK_TOKEN_LOOKUP);
                return -E_REND_TEST;
        }
        ref_put(&port->refcount, free_message_port_ref);

        port_hook_token_lookup_deny = 1;
        if (thread_lookup_port(PORT_HOOK_TOKEN_LOOKUP) != NULL) {
                pr_error("[port_hook_test] token_lookup cache resolve should deny\n");
                port_hook_token_lookup_deny = 0;
                unregister_port(global_port_table, PORT_HOOK_TOKEN_LOOKUP);
                return -E_REND_TEST;
        }
        port_hook_token_lookup_deny = 0;
        unregister_port(global_port_table, PORT_HOOK_TOKEN_LOOKUP);

        port = create_message_port(PORT_HOOK_DENY_SEND, &port_hook_test_hooks);
        if (!port) {
                pr_error("[port_hook_test] create deny_send port failed\n");
                return -E_REND_TEST;
        }
        e = register_port(global_port_table, port);
        if (e != REND_SUCCESS) {
                pr_error("[port_hook_test] register deny_send failed e=%d\n",
                         (int)e);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        ref_put(&port->refcount, free_message_port_ref);

        port = thread_lookup_port(PORT_HOOK_DENY_SEND);
        if (!port) {
                pr_error("[port_hook_test] deny_send lookup should succeed\n");
                unregister_port(global_port_table, PORT_HOOK_DENY_SEND);
                return -E_REND_TEST;
        }
        e = send_msg(port);
        ref_put(&port->refcount, free_message_port_ref);
        if (e != -E_REND_PORT_CLOSED) {
                pr_error("[port_hook_test] deny_send send_msg e=%d expected closed\n",
                         (int)e);
                unregister_port(global_port_table, PORT_HOOK_DENY_SEND);
                return -E_REND_TEST;
        }
        unregister_port(global_port_table, PORT_HOOK_DENY_SEND);

        port = create_message_port(PORT_HOOK_DENY_RECV, &port_hook_test_hooks);
        if (!port) {
                pr_error("[port_hook_test] create deny_recv port failed\n");
                return -E_REND_TEST;
        }
        e = register_port(global_port_table, port);
        if (e != REND_SUCCESS) {
                pr_error("[port_hook_test] register deny_recv failed e=%d\n",
                         (int)e);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        ref_put(&port->refcount, free_message_port_ref);

        port = thread_lookup_port(PORT_HOOK_DENY_RECV);
        if (!port) {
                pr_error("[port_hook_test] deny_recv lookup should succeed\n");
                unregister_port(global_port_table, PORT_HOOK_DENY_RECV);
                return -E_REND_TEST;
        }
        e = recv_msg(port);
        ref_put(&port->refcount, free_message_port_ref);
        if (e != -E_REND_PORT_CLOSED) {
                pr_error("[port_hook_test] deny_recv recv_msg e=%d expected closed\n",
                         (int)e);
                unregister_port(global_port_table, PORT_HOOK_DENY_RECV);
                return -E_REND_TEST;
        }
        unregister_port(global_port_table, PORT_HOOK_DENY_RECV);

        port = create_message_port(PORT_HOOK_DENY_REGISTER, &port_hook_test_hooks);
        if (!port) {
                pr_error("[port_hook_test] create deny_register port failed\n");
                return -E_REND_TEST;
        }
        e = register_port(global_port_table, port);
        if (e == REND_SUCCESS) {
                pr_error("[port_hook_test] deny_register should fail\n");
                unregister_port(global_port_table, PORT_HOOK_DENY_REGISTER);
                return -E_REND_TEST;
        }
        delete_message_port_structure(port);

        return REND_SUCCESS;
}

int single_port_test(void)
{
        Task_Manager* tm = percpu(core_tm);
        error_t e;

        port_discovery_receiver_done = 0;
        port_discovery_sender_done = 0;
        port_discovery_recv_type = -1;

        is_print_sche_info = false;

        e = gen_thread_from_func(NULL,
                                 port_discovery_receiver_thread,
                                 "port_disc_rcv",
                                 tm,
                                 NULL);
        if (e != REND_SUCCESS) {
                pr_error("[port_test] create receiver failed\n");
                return -E_REND_TEST;
        }
        e = gen_thread_from_func(
                NULL, port_discovery_sender_thread, "port_disc_snd", tm, NULL);
        if (e != REND_SUCCESS) {
                pr_error("[port_test] create sender failed\n");
                return -E_REND_TEST;
        }

        while (!port_discovery_receiver_done || !port_discovery_sender_done)
                schedule(tm);

        if (port_discovery_recv_type != PORT_DISCOVERY_MSG_TYPE) {
                pr_error("[port_test] recv type %d expected %d\n",
                         (int)port_discovery_recv_type,
                         PORT_DISCOVERY_MSG_TYPE);
                is_print_sche_info = false;
                return -E_REND_TEST;
        }
        if (strcmp(port_discovery_recv_buf, port_discovery_payload) != 0) {
                pr_error("[port_test] recv payload \"%s\" expected \"%s\"\n",
                         port_discovery_recv_buf,
                         port_discovery_payload);
                is_print_sche_info = false;
                return -E_REND_TEST;
        }

        if (thread_lookup_port(PORT_DISCOVERY_PORT_NAME) != NULL) {
                pr_error(
                        "[port_test] lookup after unregister should be NULL\n");
                is_print_sche_info = false;
                return -E_REND_TEST;
        }

        if (port_hook_gate_self_test() != REND_SUCCESS) {
                is_print_sche_info = false;
                return -E_REND_TEST;
        }

        is_print_sche_info = false;
        return REND_SUCCESS;
}
