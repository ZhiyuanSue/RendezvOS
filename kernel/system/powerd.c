#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/system/powerd.h>
#include <rendezvos/task/initcall.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/system/panic.h>

extern cpu_id_t BSP_ID;

static void* powerd_thread(void* arg)
{
        (void)arg;
        Message_Port_t* port = create_message_port(RENDEZVOS_POWERD_PORT_NAME);
        if (!port) {
                pr_error("[powerd] create port failed\n");
                return NULL;
        }
        if (register_port(global_port_table, port) != REND_SUCCESS) {
                pr_error("[powerd] register port failed\n");
                delete_message_port_structure(port);
                return NULL;
        }

        while (1) {
                if (recv_msg(port) != REND_SUCCESS) {
                        pr_error("[powerd] recv_msg failed\n");
                        continue;
                }
                Message_t* m = dequeue_recv_msg();
                if (!m) {
                        pr_error("[powerd] recv queue empty after recv_msg\n");
                        continue;
                }
                if (!m->data) {
                        pr_error("[powerd] message has no data\n");
                        ref_put(&m->ms_queue_node.refcount, free_message_ref);
                        continue;
                }
                const kmsg_t* km = kmsg_from_msg(m);
                if (!km || km->hdr.module != port->service_id) {
                        ref_put(&m->ms_queue_node.refcount, free_message_ref);
                        continue;
                }
                u16 op = km->hdr.opcode;
                ref_put(&m->ms_queue_node.refcount, free_message_ref);

                if (op == KMSG_OP_CORE_POWER_SHUTDOWN) {
                        pr_info("[powerd] shutdown request\n");
                        kernel_halt();
                } else if (op == KMSG_OP_CORE_POWER_REBOOT) {
                        pr_error("[powerd] reboot not implemented\n");
                } else {
                        pr_error("[powerd] unknown opcode %u\n", (unsigned)op);
                }
        }
}

static void powerd_init(void)
{
        if (percpu(cpu_number) != BSP_ID)
                return;
        if (!percpu(core_tm)) {
                pr_error("[powerd] core_tm not ready\n");
                return;
        }
        error_t e = gen_thread_from_func(
                NULL, powerd_thread, "powerd", percpu(core_tm), NULL);
        if (e != REND_SUCCESS)
                pr_error("[powerd] spawn thread failed e=%d\n", e);
}

DEFINE_INIT(powerd_init);
