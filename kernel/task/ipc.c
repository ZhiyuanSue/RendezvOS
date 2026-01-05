#include <rendezvos/task/ipc.h>
#include <rendezvos/task/tcb.h>

struct Msg_Port* create_message_port()
{
        struct Msg_Port* mp =
                kallocator->m_alloc(kallocator, sizeof(struct Msg_Port));
        ms_queue_node_t* idle_node =
                kallocator->m_alloc(kallocator, sizeof(struct Msg_Port));
        if (mp && idle_node) {
                msq_init(&mp->ms_queue, idle_node);
        } else {
                return NULL;
        }
        return mp;
}
struct Msg* create_message(i64 msg_type, u64 append_info_len, char* append_info)
{
        struct Msg* msg = kallocator->m_alloc(
                kallocator, sizeof(struct Msg) + append_info_len);
        if (msg) {
                msg->append_info_len = append_info_len;
                msg->msg_type = msg_type;
                memcpy(&msg->append_info, append_info, append_info_len);
        }
        return msg;
}
void send_msg(Message_Port_t* port, Message_t* message)
{
        msq_enqueue(&port->ms_queue, &message->ms_node);
}
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg(Message_Port_t* port)
{
        struct allocator* malloc;
        tagged_ptr_t dequeue_ptr = tp_new_none();
        while ((dequeue_ptr = msq_dequeue(&port->ms_queue)) == 0)
                ;
        Message_t* get_ptr =
                container_of(tp_get_ptr(dequeue_ptr), struct Msg, ms_node);
        Message_t* next_ptr = container_of(
                tp_get_ptr(get_ptr->ms_node.next), struct Msg, ms_node);
        malloc = percpu(kallocator);
        malloc->m_free(malloc, get_ptr);
        return next_ptr;
}
Message_t* recv_msg_await(Message_Port_t* port);