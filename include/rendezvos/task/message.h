#ifndef _RENDEZVOS_MESSAGE_H_
#define _RENDEZVOS_MESSAGE_H_

#include <common/types.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

/*message structure*/
#define MESSAGE_COMMON                  \
        u64 append_info_len;            \
        ms_queue_node_t msg_queue_node; \
        atomic64_t ref_count

typedef struct Msg Message_t;
struct Msg {
        MESSAGE_COMMON;
        i64 msg_type;
        char append_info[];
};

Message_t* create_message(i64 msg_type, u64 append_info_len, char* append_info);
void delete_message_structure(Message_t* msg);
void message_structure_ref_inc(Message_t* msg);
bool message_structure_ref_dec(Message_t* msg);

void clean_message_queue(ms_queue_t* ms_queue);

#endif