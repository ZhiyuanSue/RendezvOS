#ifndef _RENDEZVOS_POWERD_H_
#define _RENDEZVOS_POWERD_H_

#include <common/types.h>
#include <common/string.h>
#include <rendezvos/error.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/ipc/kmsg_system.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/thread.h>

#define RENDEZVOS_POWERD_PORT_NAME "powerd"

/**
 * @brief send KMSG_OP_SYSTEM_POWER_SHUTDOWN to "powerd" thread.
 *
 * Prefer this over @c kernel_panic for orderly teardown when powerd is
 * registered.
 *
 * @return @c send_msg result, or @c -E_RENDEZVOS if the port is missing or
 *         message construction / enqueue fails.
 */
static inline error_t rendezvos_request_poweroff(void)
{
        Message_Port_t* port = thread_lookup_port(RENDEZVOS_POWERD_PORT_NAME);
        if (!port)
                return -E_RENDEZVOS;

        Msg_Data_t* d = kmsg_create(
                port->service_id, KMSG_OP_SYSTEM_POWER_SHUTDOWN, "");
        if (!d) {
                ref_put(&port->refcount, free_message_port_ref);
                return -E_RENDEZVOS;
        }
        Message_t* m = create_message_with_msg(d);
        ref_put(&d->refcount, free_msgdata_ref_default);
        if (!m) {
                ref_put(&port->refcount, free_message_port_ref);
                return -E_RENDEZVOS;
        }
        if (enqueue_msg_for_send(m) != REND_SUCCESS) {
                ref_put(&m->ms_queue_node.refcount, free_message_ref);
                ref_put(&port->refcount, free_message_port_ref);
                return -E_RENDEZVOS;
        }
        error_t e = send_msg(port);
        ref_put(&port->refcount, free_message_port_ref);
        return e;
}

#endif
