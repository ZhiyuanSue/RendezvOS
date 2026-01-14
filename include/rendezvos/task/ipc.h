#include "../error.h"
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

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
        atomic64_t tcb_queue_cnt;

        ms_queue_t send_msg_queue;
        atomic64_t send_msg_cnt;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->send_msg_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_ENDPOINT_APPEND_BITS) - 1);
}

struct Msg_Port* create_message_port();
struct Msg* create_message(i64 msg_type, u64 append_info_len,
                           char* append_info);
void send_msg(Message_Port_t* port, Message_t* message);
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg(Message_Port_t* port);
Message_t* recv_msg_await(Message_Port_t* port);