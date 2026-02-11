#ifndef _RENDEZVOS_MESSAGE_H_
#define _RENDEZVOS_MESSAGE_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>

/*message data structure, which is uniquely hold the data*/
typedef struct MsgData Msg_Data_t;
struct MsgData {
        ref_count_t refcount;
        i64 msg_type;
        u64 data_len; /*Bytes*/
        void* data;
        void (*free_data)(ref_count_t*);
};
Msg_Data_t* create_message_data(i64 msg_type, u64 data_len, void** data_ptr,
                                void (*free_data)(ref_count_t*));
void delete_msgdata_structure(Msg_Data_t* msg_data);
void free_msgdata_ref_default(ref_count_t* msgdata_refcount);

/*
message structure,
the reseaon of using msg and msgdata structure to represent a message
is that the ms_queue_node_t have a refcount,
but msq dequeue op only dequeue the dummy node,
the real node just become the new dummy node and in the queue.
so we cannot 'move' to another queue, and I copied on msg.
This copy lead to the new data memory lifecycle problem.
and the ms_queue_node_t's refcount cannot used here to manage the lifcycle of
data memory.
So I have to split the msg and msgdata structure.
*/
typedef struct Msg Message_t;
struct Msg {
        ms_queue_node_t ms_queue_node;
        Msg_Data_t* data;
};

Message_t* create_message(Msg_Data_t* msgdata);
void free_message_ref(ref_count_t* ref_count_ptr);
void clean_message_queue(ms_queue_t* ms_queue, bool delete_dummy);

#endif