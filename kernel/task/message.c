#include <rendezvos/task/message.h>

struct Msg* create_message(i64 msg_type, u64 append_info_len,
                           char** append_info)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg* msg =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_t));
        if (msg) {
                memset(msg, 0, sizeof(Message_t));
                ref_get_claim(&msg->ms_queue_node.refcount);
                if (append_info_len > 0 && append_info) {
                        msg->append_info_len = append_info_len;
                        msg->msg_type = msg_type;
                        msg->append_info = *append_info;
                        /*after put msg into the msg structure,
                        the owner of this memory is msg structure*/
                        *append_info = NULL;
                } else {
                        /*we have to clean the msg structure*/
                        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                        return NULL;
                }
        }
        return msg;
}
void delete_message_structure(Message_t* msg)
{
        if (!msg)
                return;
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, (void*)msg);
}
void free_message_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        ms_queue_node_t* node =
                container_of(ref_count_ptr, ms_queue_node_t, refcount);
        Message_t* dummy_msg = container_of(node, Message_t, ms_queue_node);
        delete_message_structure(dummy_msg);
}

void clean_message_queue(ms_queue_t* ms_queue, bool delete_dummy)
{
        if (!ms_queue)
                return;
        tagged_ptr_t dequeued_ptr;
        while (!tp_is_none(dequeued_ptr =
                                   msq_dequeue(ms_queue, free_message_ref))) {
                ref_put(&((ms_queue_node_t*)tp_get_ptr(dequeued_ptr))->refcount,
                        free_message_ref);
        }
        if (delete_dummy) {
                tagged_ptr_t head_tp =
                        atomic64_load((volatile u64*)&ms_queue->head);
                ms_queue_node_t* dummy = (ms_queue_node_t*)tp_get_ptr(head_tp);
                if (dummy) {
                        ref_put(&dummy->refcount, free_message_ref);
                        atomic64_store((volatile u64*)&ms_queue->head, 0);
                        atomic64_store((volatile u64*)&ms_queue->tail, 0);
                }
        }
}