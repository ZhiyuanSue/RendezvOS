#include <rendezvos/task/message.h>

struct Msg* create_message(i64 msg_type, u64 append_info_len, char* append_info)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg* msg =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_t));
        if (msg) {
                message_structure_ref_inc(msg);
                msg->append_info_len = append_info_len;
                msg->msg_type = msg_type;
                msg->append_info = append_info;
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
void message_structure_ref_inc(Message_t* msg)
{
        if (!msg)
                return;
        atomic64_inc(&msg->msg_queue_node.refcount);
        barrier();
}
bool message_structure_ref_dec(Message_t* msg)
{
        if (!msg)
                return false;
        i64 old_ref_value = atomic64_fetch_dec(&msg->msg_queue_node.refcount);
        if (old_ref_value == 1) {
                delete_message_structure(msg);
                return true;
        }
        return false;
}
/* Free function for old dummy nodes in Message queues. */
static void free_message_dummy(void* node)
{
        Message_t* dummy_msg =
                container_of((ms_queue_node_t*)node, Message_t, msg_queue_node);
        delete_message_structure(dummy_msg);
}

void clean_message_queue(ms_queue_t* ms_queue, bool delete_dummy)
{
        if (!ms_queue)
                return;
        tagged_ptr_t dequeued_ptr;
        while (!tp_is_none(dequeued_ptr =
                                   msq_dequeue(ms_queue, free_message_dummy))) {
                msq_node_ref_put((ms_queue_node_t*)tp_get_ptr(dequeued_ptr),
                                 free_message_dummy);
        }
        if (delete_dummy) {
                tagged_ptr_t head_tp =
                        atomic64_load((volatile u64*)&ms_queue->head);
                ms_queue_node_t* dummy =
                        (ms_queue_node_t*)tp_get_ptr(head_tp);
                if (dummy) {
                        msq_node_ref_put(dummy, free_message_dummy);
                        atomic64_store((volatile u64*)&ms_queue->head, 0);
                        atomic64_store((volatile u64*)&ms_queue->tail, 0);
                }
        }
}