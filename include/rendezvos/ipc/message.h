#ifndef _RENDEZVOS_MESSAGE_H_
#define _RENDEZVOS_MESSAGE_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/dsa/ms_queue.h>
#include <common/string.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>

/*message data structure, which is uniquely hold the data*/
typedef struct MsgData Msg_Data_t;
struct MsgData {
        ref_count_t refcount;
        i64 msg_type;
        u64 data_len; /*Bytes*/
        void* data;
        error_t (*free_data)(ref_count_t*);
};

/**
 * @brief Allocate Msg_Data and take ownership of an existing data buffer.
 * @param msg_type Caller-defined type tag for the payload.
 * @param data_len Length of *data_ptr in bytes.
 * @param data_ptr In/out pointer to payload memory; set to NULL on success.
 * @param free_data Refcount destructor for msg_data and its data buffer.
 * @return New Msg_Data with refcount 1, or NULL on invalid input or allocation
 *         failure.
 */
Msg_Data_t* create_message_data(i64 msg_type, u64 data_len, void** data_ptr,
                                error_t (*free_data)(ref_count_t*));

/**
 * @brief Free the Msg_Data structure without touching the payload buffer.
 * @param msg_data Structure to free; no-op if NULL.
 */
void delete_msgdata_structure(Msg_Data_t* msg_data);

/**
 * @brief Default Msg_Data destructor: free data buffer then structure.
 * @param msgdata_refcount Pointer to msg_data->refcount.
 * @return REND_SUCCESS on success; -E_IN_PARAM if msgdata_refcount is NULL.
 */
error_t free_msgdata_ref_default(ref_count_t* msgdata_refcount);

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
        void* receiver;
};

/*
 * One way is using an exist msgdata (create_message_with_msg)
 * Another is create_message and then do something and finally fill_message_data
 */
/**
 * @brief Create a Message_t that shares an existing Msg_Data (refcount bumped).
 * @param msgdata Payload to attach; must have a live refcount.
 * @return New message with refcount 1, or NULL if msgdata is invalid or
 *         allocation fails.
 */
Message_t* create_message_with_msg(Msg_Data_t* msgdata);

/**
 * @brief Allocate an empty Message_t without payload.
 * @return New message with refcount 1, or NULL on allocation failure.
 */
Message_t* create_message_structure(void);

/**
 * @brief Free the Message_t structure without releasing attached Msg_Data.
 * @param msg Message to free; no-op if NULL.
 */
void delete_message_structure(Message_t* msg);

/**
 * @brief Attach Msg_Data to a message (refcount bumped).
 * @param msg Message to fill; must not be NULL.
 * @param msgdata Payload to attach; must have a live refcount.
 * @return REND_SUCCESS on success; -E_IN_PARAM on invalid input.
 */
error_t fill_message_data(Message_t* msg, Msg_Data_t* msgdata);

/**
 * @brief Refcount destructor for Message_t; defers reclaim via EBR then frees
 * attached Msg_Data.
 * @param ref_count_ptr Pointer to msg->ms_queue_node.refcount.
 * @return REND_SUCCESS on success; -E_IN_PARAM if ref_count_ptr is NULL.
 */
error_t free_message_ref(ref_count_t* ref_count_ptr);

#endif
