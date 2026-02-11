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

#define IPC_TEST_MSG_TYPE 42

static volatile int single_ipc_sender_done;
static volatile int single_ipc_receiver_done;
static i64 single_ipc_received_type;
static char single_ipc_send_payload[32] __attribute__((aligned(16))) = "ipc_hello";
static char multi_ipc_send_payload[32] __attribute__((aligned(16))) = "round_";
static char single_ipc_received_payload[32] __attribute__((aligned(16)));

static void* ipc_sender_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        char* payload = (char*)percpu(kallocator)
                                ->m_alloc(percpu(kallocator),
                                          sizeof(single_ipc_send_payload));
        if (!payload) {
                pr_error("[single_ipc_test] sender: payload alloc failed\n");
                single_ipc_sender_done = 1;
                return NULL;
        }
        memcpy(payload,
               single_ipc_send_payload,
               sizeof(single_ipc_send_payload));
        void* payload_ptr = payload;

        Msg_Data_t* msgdata =
                create_message_data(IPC_TEST_MSG_TYPE,
                                    sizeof(single_ipc_send_payload),
                                    &payload_ptr,
                                    free_msgdata_ref_default);
        if (!msgdata) {
                pr_error(
                        "[single_ipc_test] sender: create_message_data failed\n");
                percpu(kallocator)->m_free(percpu(kallocator), payload);
                single_ipc_sender_done = 1;
                return NULL;
        }
        Message_t* msg = create_message(msgdata);
        if (!msg) {
                pr_error("[single_ipc_test] sender: create_message failed\n");
                single_ipc_sender_done = 1;
                return NULL;
        }
        /* After create_message, we can release our reference to msgdata */
        ref_put(&msgdata->refcount, free_msgdata_ref_default);
        msgdata = NULL;

        if (enqueue_msg_for_send(msg, false) != REND_SUCCESS) {
                pr_error(
                        "[single_ipc_test] sender: enqueue_msg_for_send failed\n");
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                single_ipc_sender_done = 1;
                return NULL;
        }
        if (send_msg(port) != REND_SUCCESS) {
                pr_error("[single_ipc_test] sender: send_msg failed\n");
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
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
        if (!msg->data) {
                pr_error("[single_ipc_test] receiver: msg->data is NULL\n");
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                single_ipc_receiver_done = 1;
                return NULL;
        }
        single_ipc_received_type = msg->data->msg_type;
        u64 data_len = msg->data->data_len;
        if (data_len > 0 && msg->data->data) {
                u64 copy_len =
                        (data_len < sizeof(single_ipc_received_payload) - 1) ?
                                data_len :
                                sizeof(single_ipc_received_payload) - 1;
                memcpy(single_ipc_received_payload, msg->data->data, copy_len);
                single_ipc_received_payload[copy_len] = '\0';
        } else {
                single_ipc_received_payload[0] = '\0';
        }
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
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
        memset(single_ipc_received_payload,
               0,
               sizeof(single_ipc_received_payload));

        port = create_message_port();
        if (!port) {
                pr_error("[single_ipc_test] create_message_port failed\n");
                return -E_REND_TEST;
        }

        e = gen_thread_from_func(
                NULL, ipc_sender_thread, "ipc_sender", tm, port);
        if (e) {
                delete_message_port(port);
                return -E_REND_TEST;
        }
        e = gen_thread_from_func(
                NULL, ipc_receiver_thread, "ipc_receiver", tm, port);
        if (e) {
                delete_message_port(port);
                return -E_REND_TEST;
        }

        while (!single_ipc_sender_done || !single_ipc_receiver_done)
                schedule(tm);

        delete_message_port(port);

        if (single_ipc_received_type != IPC_TEST_MSG_TYPE) {
                pr_error(
                        "[single_ipc_test] received msg_type %d, expected %d\n",
                        (int)single_ipc_received_type,
                        (int)IPC_TEST_MSG_TYPE);
                return -E_REND_TEST;
        }
        if (strcmp(single_ipc_received_payload, single_ipc_send_payload) != 0) {
                pr_error(
                        "[single_ipc_test] received payload \"%s\", expected \"%s\"\n",
                        single_ipc_received_payload,
                        single_ipc_send_payload);
                return -E_REND_TEST;
        }
        return REND_SUCCESS;
}

#define IPC_MULTI_ROUND_COUNT 500

static volatile int multi_round_sender_done;
static volatile int multi_round_receiver_done;
static volatile u64 multi_round_send_count;
static volatile u64 multi_round_recv_count;
static volatile i64 multi_round_last_recv_type;

static void* ipc_multi_round_sender_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        multi_round_send_count = 0;

        for (u64 i = 0; i < IPC_MULTI_ROUND_COUNT; i++) {
                /* Use simple payload: allocate on heap to avoid stack reuse
                 * issues */
                char* payload =
                        (char*)percpu(kallocator)
                                ->m_alloc(percpu(kallocator),
                                          sizeof(multi_ipc_send_payload));
                if (!payload) {
                        pr_error(
                                "[single_ipc_multi_round_test] sender: payload alloc failed at round %llu\n",
                                (unsigned long long)i);
                        break;
                }

                /* Fill payload with round number (simple format) */
                memcpy(payload,
                       multi_ipc_send_payload,
                       sizeof(multi_ipc_send_payload));

                /* Convert i to string manually */
                u64 num = i;
                int pos = 6;
                if (num == 0) {
                        payload[pos++] = '0';
                } else {
                        char digits[16];
                        int digit_count = 0;
                        while (num > 0 && digit_count < 15) {
                                digits[digit_count++] = '0' + (num % 10);
                                num /= 10;
                        }
                        for (int j = digit_count - 1; j >= 0 && pos < 15; j--) {
                                payload[pos++] = digits[j];
                        }
                }
                payload[pos] = '\0';
                void* payload_ptr = payload;

                Msg_Data_t* msgdata = create_message_data(
                        (i64)i, 16, &payload_ptr, free_msgdata_ref_default);
                if (!msgdata) {
                        pr_error(
                                "[single_ipc_multi_round_test] sender: create_message_data failed at round %llu\n",
                                (unsigned long long)i);
                        percpu(kallocator)->m_free(percpu(kallocator), payload);
                        break;
                }

                // pr_info("5\n");

                Message_t* msg = create_message(msgdata);
                if (!msg) {
                        pr_error(
                                "[single_ipc_multi_round_test] sender: create_message failed at round %llu\n",
                                (unsigned long long)i);
                        ref_put(&msgdata->refcount, free_msgdata_ref_default);
                        break;
                }

                // pr_info("6\n");
                /* After create_message, we can release our reference to msgdata
                 */
                ref_put(&msgdata->refcount, free_msgdata_ref_default);

                // pr_info("7\n");
                if (enqueue_msg_for_send(msg, false) != REND_SUCCESS) {
                        pr_error(
                                "[single_ipc_multi_round_test] sender: enqueue_msg_for_send failed at round %llu\n",
                                (unsigned long long)i);
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        break;
                }

                // pr_info("8\n");

                if (send_msg(port) != REND_SUCCESS) {
                        pr_error(
                                "[single_ipc_multi_round_test] sender: send_msg failed at round %llu\n",
                                (unsigned long long)i);
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        break;
                }

                // pr_info("9\n");

                multi_round_send_count++;

                if ((i + 1) % 100 == 0) {
                        pr_info("[single_ipc_multi_round_test] sender: sent %u/%u messages\n",
                                (unsigned long long)(i + 1),
                                IPC_MULTI_ROUND_COUNT);
                }
        }

        multi_round_sender_done = 1;
        pr_info("[single_ipc_multi_round_test] sender: completed, total sent=%u\n",
                (unsigned long long)multi_round_send_count);
        return NULL;
}

static void* ipc_multi_round_receiver_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        multi_round_recv_count = 0;
        multi_round_last_recv_type = -1;

        for (u64 i = 0; i < IPC_MULTI_ROUND_COUNT; i++) {
                if (recv_msg(port) != REND_SUCCESS) {
                        pr_error(
                                "[single_ipc_multi_round_test] receiver: recv_msg failed at round %u\n",
                                (unsigned long long)i);
                        break;
                }

                Message_t* msg = dequeue_recv_msg();
                if (!msg) {
                        pr_error(
                                "[single_ipc_multi_round_test] receiver: recv_queue empty at round %u\n",
                                (unsigned long long)i);
                        break;
                }

                if (!msg->data) {
                        pr_error(
                                "[single_ipc_multi_round_test] receiver: msg->data is NULL at round %u\n",
                                (unsigned long long)i);
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        break;
                }

                multi_round_last_recv_type = msg->data->msg_type;
                multi_round_recv_count++;

                ref_put(&msg->ms_queue_node.refcount, free_message_ref);

                if ((i + 1) % 100 == 0) {
                        pr_info("[single_ipc_multi_round_test] receiver: received %u/%u messages\n",
                                (unsigned long long)(i + 1),
                                IPC_MULTI_ROUND_COUNT);
                }
        }

        multi_round_receiver_done = 1;
        pr_info("[single_ipc_multi_round_test] receiver: completed, total received=%u\n",
                (unsigned long long)multi_round_recv_count);
        return NULL;
}

int ipc_multi_round_test(void)
{
        Message_Port_t* port;
        Task_Manager* tm = percpu(core_tm);
        error_t e;

        multi_round_sender_done = 0;
        multi_round_receiver_done = 0;
        multi_round_send_count = 0;
        multi_round_recv_count = 0;
        multi_round_last_recv_type = -1;

        pr_info("[single_ipc_multi_round_test] starting multi-round IPC test (%u rounds)\n",
                IPC_MULTI_ROUND_COUNT);

        port = create_message_port();
        if (!port) {
                pr_error(
                        "[single_ipc_multi_round_test] create_message_port failed\n");
                return -E_REND_TEST;
        }

        e = gen_thread_from_func(NULL,
                                 ipc_multi_round_sender_thread,
                                 "ipc_multi_sender",
                                 tm,
                                 port);
        if (e) {
                pr_error(
                        "[single_ipc_multi_round_test] failed to create sender thread\n");
                delete_message_port(port);
                return -E_REND_TEST;
        }

        e = gen_thread_from_func(NULL,
                                 ipc_multi_round_receiver_thread,
                                 "ipc_multi_receiver",
                                 tm,
                                 port);
        if (e) {
                pr_error(
                        "[single_ipc_multi_round_test] failed to create receiver thread\n");
                delete_message_port(port);
                return -E_REND_TEST;
        }

        while (!multi_round_sender_done || !multi_round_receiver_done)
                schedule(tm);

        delete_message_port(port);

        pr_info("[single_ipc_multi_round_test] test completed: sent=%u received=%u\n",
                (unsigned long long)multi_round_send_count,
                (unsigned long long)multi_round_recv_count);

        if (multi_round_send_count != IPC_MULTI_ROUND_COUNT) {
                pr_error(
                        "[single_ipc_multi_round_test] sender count mismatch: got %u, expected %u\n",
                        (unsigned long long)multi_round_send_count,
                        IPC_MULTI_ROUND_COUNT);
                return -E_REND_TEST;
        }

        if (multi_round_recv_count != IPC_MULTI_ROUND_COUNT) {
                pr_error(
                        "[single_ipc_multi_round_test] receiver count mismatch: got %u, expected %u\n",
                        (unsigned long long)multi_round_recv_count,
                        IPC_MULTI_ROUND_COUNT);
                return -E_REND_TEST;
        }

        if (multi_round_last_recv_type != (i64)(IPC_MULTI_ROUND_COUNT - 1)) {
                pr_error(
                        "[single_ipc_multi_round_test] last received type mismatch: got %d, expected %u\n",
                        (long long)multi_round_last_recv_type,
                        IPC_MULTI_ROUND_COUNT - 1);
                return -E_REND_TEST;
        }

        pr_info("[single_ipc_multi_round_test] PASS: all %u messages sent and received correctly\n",
                IPC_MULTI_ROUND_COUNT);
        return REND_SUCCESS;
}
