/*
 * Multi-core IPC test: even-numbered CPUs act as senders, odd-numbered as
 * receivers, sharing one Message_Port_t. Each sender sends a fixed number
 * of messages; each receiver receives the same number. Verifies lock-free
 * IPC across cores.
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/message.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/kmalloc.h>
#include <common/string.h>

extern struct allocator* kallocator;

extern u32 BSP_ID;
extern int NR_CPU;

#define SMP_IPC_MSG_COUNT 500

static Message_Port_t* smp_ipc_port;
static volatile bool smp_ipc_port_ready;
static volatile u64 smp_ipc_send_ok[RENDEZVOS_MAX_CPU_NUMBER];
static volatile u64 smp_ipc_recv_ok[RENDEZVOS_MAX_CPU_NUMBER];

static void free_payload_data(ref_count_t* msgdata_refcount)
{
        if (!msgdata_refcount)
                return;
        Msg_Data_t* msg_data =
                container_of(msgdata_refcount, Msg_Data_t, refcount);
        if (msg_data->data) {
                struct allocator* cpu_kallocator = percpu(kallocator);
                cpu_kallocator->m_free(cpu_kallocator, msg_data->data);
        }
        delete_msgdata_structure(msg_data);
}

static void smp_ipc_sender_loop(u32 cpu_id, int count)
{
        pr_info("cpu %u sender start, count=%d\n", (unsigned)cpu_id, count);
        for (int i = 0; i < count; i++) {
                /* unique msg_type per (cpu, index) for optional check */
                i64 msg_type = (i64)((u64)cpu_id * 10000 + (u64)i);

                /* Allocate payload on heap */
                char* payload = (char*)percpu(kallocator)
                                        ->m_alloc(percpu(kallocator), 16);
                if (!payload) {
                        pr_info("cpu %u sender payload alloc failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }

                /* Fill payload with msg_type as string */
                int len = 0;
                i64 num = msg_type;
                if (num < 0) {
                        payload[len++] = '-';
                        num = -num;
                }
                if (num == 0) {
                        payload[len++] = '0';
                } else {
                        char digits[16];
                        int digit_count = 0;
                        while (num > 0 && digit_count < 15) {
                                digits[digit_count++] = '0' + (num % 10);
                                num /= 10;
                        }
                        for (int j = digit_count - 1; j >= 0 && len < 15; j--) {
                                payload[len++] = digits[j];
                        }
                }
                payload[len] = '\0';

                void* payload_ptr = payload;
                Msg_Data_t* msgdata = create_message_data(
                        msg_type, (u64)len, &payload_ptr, free_payload_data);
                if (!msgdata) {
                        pr_info("cpu %u sender create_message_data failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        percpu(kallocator)->m_free(percpu(kallocator), payload);
                        break;
                }

                Message_t* msg = create_message(msgdata);
                if (!msg) {
                        pr_info("cpu %u sender create_message failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        ref_put(&msgdata->refcount, free_payload_data);
                        break;
                }
                /* After create_message, we can release our reference to msgdata
                 */
                ref_put(&msgdata->refcount, free_payload_data);

                if (enqueue_msg_for_send(msg, false) != REND_SUCCESS) {
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        pr_info("cpu %u sender enqueue_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                // pr_info("cpu %u sender calling send_msg #%d\n",
                //         (unsigned)cpu_id,
                //         i);
                if (send_msg(smp_ipc_port) != REND_SUCCESS) {
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        pr_info("cpu %u sender send_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                // pr_info("cpu %u sender send_msg returned SUCCESS #%d\n",
                //         (unsigned)cpu_id,
                //         i);
                smp_ipc_send_ok[cpu_id]++;
        }
        pr_info("cpu %u sender done, total=%u\n",
                (unsigned)cpu_id,
                (unsigned long long)smp_ipc_send_ok[cpu_id]);
}

static void smp_ipc_receiver_loop(u32 cpu_id, int count)
{
        pr_info("cpu %u receiver start, count=%d\n", (unsigned)cpu_id, count);
        for (int i = 0; i < count; i++) {
                // pr_info("cpu %u receiver calling recv_msg #%d\n",
                //         (unsigned)cpu_id,
                //         i);
                if (recv_msg(smp_ipc_port) != REND_SUCCESS) {
                        pr_info("cpu %u receiver recv_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                // pr_info("cpu %u receiver recv_msg returned SUCCESS #%d\n",
                //         (unsigned)cpu_id,
                //         i);
                Message_t* msg = dequeue_recv_msg();
                if (!msg) {
                        pr_info("cpu %u receiver dequeue_recv_msg NULL at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                if (!msg->data) {
                        pr_info("cpu %u receiver msg->data is NULL at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        break;
                }
                // pr_info("[smp_ipc_test] cpu %u recv #%d msg_type=%d\n",
                //         (unsigned)cpu_id,
                //         i,
                //         (int)msg->data->msg_type);
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                smp_ipc_recv_ok[cpu_id]++;
        }
        pr_info("cpu %u receiver done, total=%u\n",
                (unsigned)cpu_id,
                (unsigned long long)smp_ipc_recv_ok[cpu_id]);
}

int smp_ipc_test(void)
{
        u32 cpu_id = percpu(cpu_number);
        is_print_sche_info = false;

        if (cpu_id == BSP_ID) {
                pr_info("BSP creating message port\n");
                smp_ipc_port = create_message_port();
                if (!smp_ipc_port) {
                        pr_error("[smp_ipc_test] create_message_port failed\n");
                        return -E_REND_TEST;
                }
                for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                        smp_ipc_send_ok[i] = 0;
                        smp_ipc_recv_ok[i] = 0;
                }
                smp_ipc_port_ready = true;
                pr_info("BSP port ready, NR_CPU=%d\n", NR_CPU);
        } else {
                pr_info("cpu %u waiting for port_ready\n", (unsigned)cpu_id);
                while (!smp_ipc_port_ready)
                        ;
                pr_info("cpu %u port ready\n", (unsigned)cpu_id);
        }

        if (cpu_id % 2 == 0) {
                /* sender: send SMP_IPC_MSG_COUNT messages */
                smp_ipc_sender_loop(cpu_id, SMP_IPC_MSG_COUNT);
        } else {
                /* receiver: receive SMP_IPC_MSG_COUNT messages */
                smp_ipc_receiver_loop(cpu_id, SMP_IPC_MSG_COUNT);
        }

        if (cpu_id == BSP_ID) {
                /* wait for all CPUs to finish then check counts */
                pr_info("BSP entering all_done wait loop\n");
                while (1) {
                        bool all_done = true;
                        for (int i = 0; i < NR_CPU; i++) {
                                u64 want = (u64)SMP_IPC_MSG_COUNT;
                                u64 s = smp_ipc_send_ok[i];
                                u64 r = smp_ipc_recv_ok[i];
                                if (i % 2 == 0) {
                                        if (s < want)
                                                all_done = false;
                                } else {
                                        if (r < want)
                                                all_done = false;
                                }
                        }
                        if (all_done)
                                break;
                }
                pr_info("BSP all_done, exiting wait\n");
                delete_message_port(smp_ipc_port);
                smp_ipc_port_ready = false;

                u64 total_sent = 0;
                u64 total_recv = 0;
                for (int i = 0; i < NR_CPU; i++) {
                        total_sent += smp_ipc_send_ok[i];
                        total_recv += smp_ipc_recv_ok[i];
                }
                if (total_sent != total_recv) {
                        pr_error(
                                "[smp_ipc_test] total_sent %u != total_recv %u\n",
                                (unsigned long long)total_sent,
                                (unsigned long long)total_recv);
                        return -E_REND_TEST;
                }
        }
        is_print_sche_info = true;
        return REND_SUCCESS;
}
