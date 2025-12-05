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
        ms_queue_t ms_queue;
};