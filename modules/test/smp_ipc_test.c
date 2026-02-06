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
#include <common/string.h>

extern u32 BSP_ID;
extern int NR_CPU;

#define SMP_IPC_MSG_COUNT 500

static Message_Port_t* smp_ipc_port;
static volatile bool smp_ipc_port_ready;
static volatile u64 smp_ipc_send_ok[RENDEZVOS_MAX_CPU_NUMBER];
static volatile u64 smp_ipc_recv_ok[RENDEZVOS_MAX_CPU_NUMBER];

static void smp_ipc_sender_loop(u32 cpu_id, int count)
{
        char payload[16];
        for (int i = 0; i < count; i++) {
                /* unique msg_type per (cpu, index) for optional check */
                i64 msg_type = (i64)((u64)cpu_id * 10000 + (u64)i);
                int len = 0;
                for (int j = 0; j < 8 && msg_type != 0; j++) {
                        payload[len++] = (char)('0' + (msg_type % 10));
                        msg_type /= 10;
                }
                if (len == 0)
                        payload[len++] = '0';

                Message_t* msg = create_message(
                        (i64)((u64)cpu_id * 10000 + (u64)i),
                        (u64)len,
                        payload);
                if (!msg)
                        break;
                if (enqueue_msg_for_send(msg) != REND_SUCCESS) {
                        message_structure_ref_dec(msg);
                        break;
                }
                if (send_msg(smp_ipc_port) != REND_SUCCESS) {
                        message_structure_ref_dec(msg);
                        break;
                }
                smp_ipc_send_ok[cpu_id]++;
        }
}

static void smp_ipc_receiver_loop(u32 cpu_id, int count)
{
        for (int i = 0; i < count; i++) {
                if (recv_msg(smp_ipc_port) != REND_SUCCESS)
                        break;
                Message_t* msg = dequeue_recv_msg();
                if (!msg)
                        break;
                pr_info("[smp_ipc_test] cpu %u recv #%d msg_type=%d\n",
                        (unsigned)cpu_id, i, (int)msg->msg_type);
                msq_node_ref_put(&msg->msg_queue_node, NULL);
                message_structure_ref_dec(msg);
                smp_ipc_recv_ok[cpu_id]++;
        }
}

int smp_ipc_test(void)
{
        u32 cpu_id = percpu(cpu_number);

        if (cpu_id == BSP_ID) {
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
        } else {
                while (!smp_ipc_port_ready)
                        ;
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
                delete_message_port(smp_ipc_port);
                smp_ipc_port_ready = false;

                u64 total_sent = 0;
                u64 total_recv = 0;
                for (int i = 0; i < NR_CPU; i++) {
                        total_sent += smp_ipc_send_ok[i];
                        total_recv += smp_ipc_recv_ok[i];
                }
                if (total_sent != total_recv) {
                        pr_error("[smp_ipc_test] total_sent %llu != total_recv %llu\n",
                                 (unsigned long long)total_sent,
                                 (unsigned long long)total_recv);
                        return -E_REND_TEST;
                }
        }

        return REND_SUCCESS;
}
