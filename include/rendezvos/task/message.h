#ifndef _RENDEZVOS_MESSAGE_H_
#define _RENDEZVOS_MESSAGE_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

/*message structure*/
typedef struct Msg Message_t;
struct Msg {
        ms_queue_node_t ms_queue_node;
        i64 msg_type;
        u64 append_info_len;
        char* append_info;
};

Message_t* create_message(i64 msg_type, u64 append_info_len,
                          char** append_info);
void free_message_ref(ref_count_t* ref_count_ptr);
void clean_message_queue(ms_queue_t* ms_queue, bool delete_dummy);

#endif