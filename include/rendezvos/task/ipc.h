#include "../error.h"
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

/*message structure*/
#define MESSAGE_COMMON       \
        i64 allocator_id;    \
        u64 append_info_len; \
        ms_queue_node_t ms_node

typedef struct Msg Message_t;
struct Msg {
        MESSAGE_COMMON;
        i64 msg_type;
        char append_info[];
};

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        i64 allocator_id;
        ms_queue_t ms_queue;
        atomic64_t msg_cnt;
};

struct Msg_Port* create_message_port();
struct Msg* create_message(i64 msg_type,u64 append_info_len, char* append_info);
void send_msg(Message_Port_t* port, Message_t* message);
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg(Message_Port_t* port);
Message_t* recv_msg_await(Message_Port_t* port);