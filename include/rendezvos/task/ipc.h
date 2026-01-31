#ifndef _RENDEZVOS_IPC_H_
#define _RENDEZVOS_IPC_H_

#include "../error.h"
#include "message.h"
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

#include "tcb.h"

#define IPC_ENDPOINT_APPEND_BITS 2
#define IPC_ENDPOINT_STATE_EMPTY 0
#define IPC_ENDPOINT_STATE_SEND  1
#define IPC_ENDPOINT_STATE_RECV  2

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t thread_queue;
        /*
        we must record the tcb_queue's dummy_node_ptr,and when we try to free
        the port,we also free this ptr,because other node in this queue,is
        inside the tcb base structure, and they are not always delete after they
        dequeue, but if the dummy is not record, we must try to free the dummy
        node, otherwise this space will lead to a memory leak.
        but this step is not the same when we handle the message, because the
        message always free directly.
        */
        Thread_Base* thread_queue_dummy_node_ptr;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_ENDPOINT_APPEND_BITS) - 1);
}

struct Msg_Port* create_message_port();
void delete_message_port(Message_Port_t* port);

error_t ipc_transfer_message(Thread_Base* sender, Thread_Base* receiver);
error_t send_msg(Message_Port_t* port);
error_t recv_msg(Message_Port_t* port);
error_t cancel_ipc(Thread_Base* target_thread);

#endif