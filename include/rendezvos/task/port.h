#ifndef _RENDEZVOS_PORT_H_
#define _RENDEZVOS_PORT_H_

#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t thread_queue;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

Message_Port_t* create_message_port();
void delete_message_port(Message_Port_t* port);

#endif