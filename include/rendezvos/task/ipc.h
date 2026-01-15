#include "../error.h"
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

#include "tcb.h"

/*message structure*/
#define MESSAGE_COMMON       \
        u64 append_info_len; \
        ms_queue_node_t msg_queue_node

typedef struct Msg Message_t;
struct Msg {
        MESSAGE_COMMON;
        i64 msg_type;
        char append_info[];
};

#define IPC_ENDPOINT_APPEND_BITS 2
#define IPC_ENDPOINT_STATE_EMPTY 0
#define IPC_ENDPOINT_STATE_SEND  1
#define IPC_ENDPOINT_STATE_RECV  2

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t tcb_queue;
        /*
        we must record the tcb_queue's dummy_node_ptr,and when we try to free
        the port,we also free this ptr,because other node in this queue,is
        inside the tcb base structure, and they are not always delete after they
        dequeue, but if the dummy is not record, we must try to free the dummy
        node, otherwise this space will lead to a memory leak.
        but this step is not the same when we handle the message, because the
        message always free directly.
        */
        Task_Ipc_Base* tcb_queue_dummy_node_ptr;
        ms_queue_t send_msg_queue;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->send_msg_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_ENDPOINT_APPEND_BITS) - 1);
}

struct Msg_Port* create_message_port();
void delete_message_port(Message_Port_t* port);
struct Msg* create_message(i64 msg_type, u64 append_info_len,
                           char* append_info);
error_t send_msg(Message_Port_t* port, Message_t* message);
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg(Message_Port_t* port);
Message_t* recv_msg_await(Message_Port_t* port);