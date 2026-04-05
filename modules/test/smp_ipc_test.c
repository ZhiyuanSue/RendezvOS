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

extern cpu_id_t BSP_ID;
extern int NR_CPU;

#define SMP_IPC_MSG_COUNT 50000

static Message_Port_t* smp_ipc_port;
static volatile bool smp_ipc_port_ready;
static volatile int smp_ipc_active_cpus;
/*
 * Test robustness: build-time RENDEZVOS_MAX_CPU_NUMBER can become 1 when NR_CPUS
 * is defined as 1 (e.g. some aarch64 SMP configs), while the runtime may still
 * bring up cpu_id 1. Use a floor of 2 slots and always bounds-check cpu_id.
 */
#define SMP_IPC_CPU_SLOTS \
        ((RENDEZVOS_MAX_CPU_NUMBER) < 2 ? 2 : (RENDEZVOS_MAX_CPU_NUMBER))
static volatile u64 smp_ipc_send_ok[SMP_IPC_CPU_SLOTS];
static volatile u64 smp_ipc_recv_ok[SMP_IPC_CPU_SLOTS];

static error_t free_payload_data(ref_count_t* msgdata_refcount)
{
        if (!msgdata_refcount)
                return -E_IN_PARAM;
        Msg_Data_t* msg_data =
                container_of(msgdata_refcount, Msg_Data_t, refcount);
        if (msg_data->data) {
                struct allocator* cpu_kallocator = percpu(kallocator);
                cpu_kallocator->m_free(cpu_kallocator, msg_data->data);
        }
        delete_msgdata_structure(msg_data);
        return REND_SUCCESS;
}

static void smp_ipc_sender_loop(cpu_id_t cpu_id, int count)
{
        pr_info("cpu %lu sender start, count=%d\n", cpu_id, count);
        if ((u64)cpu_id >= (u64)SMP_IPC_CPU_SLOTS) {
                pr_error("[smp_ipc_test] sender cpu_id %lu out of slots=%u\n",
                         (unsigned long)cpu_id,
                         (unsigned)SMP_IPC_CPU_SLOTS);
                return;
        }
        for (int i = 0; i < count; i++) {
                if (i % (count / 10) == 0) {
                        pr_info("cpu %d finish smp ipc send %d/%d\n",
                                cpu_id,
                                i,
                                count);
                }
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

                Message_t* msg = create_message_with_msg(msgdata);
                if (!msg) {
                        pr_info("cpu %u sender create_message_with_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        ref_put(&msgdata->refcount, free_payload_data);
                        break;
                }
                /* After create_message_with_msg, we can release our reference
                 * to msgdata
                 */
                ref_put(&msgdata->refcount, free_payload_data);

                if (enqueue_msg_for_send(msg) != REND_SUCCESS) {
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        pr_info("cpu %u sender enqueue_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                if (send_msg(smp_ipc_port) != REND_SUCCESS) {
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        pr_info("cpu %u sender send_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                smp_ipc_send_ok[cpu_id]++;
        }
        pr_info("cpu %u sender done, total=%llu\n",
                (unsigned)cpu_id,
                (unsigned long long)smp_ipc_send_ok[cpu_id]);
}

static void smp_ipc_receiver_loop(cpu_id_t cpu_id, int count)
{
        pr_info("cpu %u receiver start, count=%d\n", (unsigned)cpu_id, count);
        if ((u64)cpu_id >= (u64)SMP_IPC_CPU_SLOTS) {
                pr_error("[smp_ipc_test] receiver cpu_id %u out of slots=%u\n",
                         (unsigned)cpu_id,
                         (unsigned)SMP_IPC_CPU_SLOTS);
                return;
        }
        for (int i = 0; i < count; i++) {
                if (i % (count / 10) == 0) {
                        pr_info("cpu %d finish smp ipc recv %d/%d\n",
                                cpu_id,
                                i,
                                count);
                }
                if (recv_msg(smp_ipc_port) != REND_SUCCESS) {
                        pr_info("cpu %u receiver recv_msg failed at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                Message_t* msg = dequeue_recv_msg();
                if (!msg) {
                        pr_info("cpu %u receiver dequeue_recv_msg NULL at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        break;
                }
                if (!msg->data) {
                        pr_info("cpu %lu receiver msg->data is NULL at i=%d\n",
                                (unsigned)cpu_id,
                                i);
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        break;
                }
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                smp_ipc_recv_ok[cpu_id]++;
        }
        pr_info("cpu %u receiver done, total=%u\n",
                (unsigned)cpu_id,
                (unsigned long long)smp_ipc_recv_ok[cpu_id]);
}

int smp_ipc_test(void)
{
        cpu_id_t cpu_id = percpu(cpu_number);

        if (cpu_id == BSP_ID) {
                is_print_sche_info = false;
                pr_info("BSP creating message port\n");
                smp_ipc_port = create_message_port("smp_ipc_port");
                if (!smp_ipc_port) {
                        pr_error("[smp_ipc_test] create_message_port failed\n");
                        return -E_REND_TEST;
                }
                for (int i = 0; i < SMP_IPC_CPU_SLOTS; i++) {
                        smp_ipc_send_ok[i] = 0;
                        smp_ipc_recv_ok[i] = 0;
                }
                /*
                 * This test requires sender/receiver pairing. If NR_CPU is odd,
                 * exclude the last CPU from participating to keep an even
                 * active CPU count and avoid deadlock.
                 */
                int nr_cpu = NR_CPU;
                if (nr_cpu > SMP_IPC_CPU_SLOTS)
                        nr_cpu = SMP_IPC_CPU_SLOTS;
                if (nr_cpu < 2) {
                        smp_ipc_active_cpus = 0;
                } else {
                        smp_ipc_active_cpus = nr_cpu - (nr_cpu % 2);
                }
                smp_ipc_port_ready = true;
                pr_info("BSP port ready, NR_CPU=%d active=%d\n",
                        NR_CPU,
                        smp_ipc_active_cpus);
        } else {
                pr_info("cpu %u waiting for port_ready\n", (unsigned)cpu_id);
                while (!smp_ipc_port_ready)
                        arch_cpu_relax();
                pr_info("cpu %u port ready\n", (unsigned)cpu_id);
        }

        /* active_cpus is forced to an even number to keep sender/receiver
         * paired. CPUs with cpu_id >= active_cpus will skip the test.
         */
        int active_cpus = smp_ipc_active_cpus;
        if (active_cpus < 2) {
                if (cpu_id == BSP_ID) {
                        delete_message_port_structure(smp_ipc_port);
                        smp_ipc_port_ready = false;
                        is_print_sche_info = false;
                }
                return REND_SUCCESS;
        }
        if ((int)cpu_id >= active_cpus)
                return REND_SUCCESS;

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
                        for (int i = 0; i < active_cpus; i++) {
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
                        arch_cpu_relax();
                }
                pr_info("BSP all_done, exiting wait\n");
                delete_message_port_structure(smp_ipc_port);
                smp_ipc_port_ready = false;

                u64 total_sent = 0;
                u64 total_recv = 0;
                for (int i = 0; i < active_cpus; i++) {
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
                is_print_sche_info = false;
        }
        return REND_SUCCESS;
}
