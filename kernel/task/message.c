#include <rendezvos/task/message.h>

struct Msg* create_message(i64 msg_type, u64 append_info_len, char* append_info)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Msg* msg = cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(struct Msg) + append_info_len);
        if (msg) {
                message_structure_ref_inc(msg);
                msg->append_info_len = append_info_len;
                msg->msg_type = msg_type;
                if (append_info)
                        memcpy(&msg->append_info, append_info, append_info_len);
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
        atomic64_inc(&msg->ref_count);
        barrier();
}
bool message_structure_ref_dec(Message_t* msg)
{
        if (!msg)
                return false;
        i64 old_ref_value = atomic64_fetch_dec(&msg->ref_count);
        if (old_ref_value == 1) {
                delete_message_structure(msg);
                return true;
        }
        return false;
}
void clean_message_queue(ms_queue_t* ms_queue)
{
        if (!ms_queue)
                return;
        tagged_ptr_t dequeued_ptr;
        Message_t* clean_msg_ptr;
        while (!tp_is_none(dequeued_ptr = msq_dequeue(ms_queue))) {
                ms_queue_node_t* dequeued_node =
                        (ms_queue_node_t*)tp_get_ptr(dequeued_ptr);
                clean_msg_ptr =
                        container_of(dequeued_node, Message_t, msg_queue_node);
                message_structure_ref_dec(clean_msg_ptr);
        }
}