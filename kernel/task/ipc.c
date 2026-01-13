#include <rendezvos/task/ipc.h>
#include <rendezvos/task/tcb.h>

struct Msg_Port* create_message_port()
{
        struct Msg_Port* mp =
                kallocator->m_alloc(kallocator, sizeof(struct Msg_Port));
        if (mp) {
                /*TODO: maybe need to init the header idle msg and the header
                 * tcb*/
                msq_init(&mp->tcb_queue, &mp->tcb_queue_dummy_node,IPC_ENDPOINT_APPEND_BITS);
                msq_init(&mp->send_msg_queue, &mp->send_msg_queue_dummy_node,IPC_ENDPOINT_APPEND_BITS);
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
        Thread_Base* send_thread = get_cpu_current_thread();
        /*TODO*/
        (void)send_thread;
        (void)message;
        while(1){
                u16 tcb_queue_state = ipc_get_queue_state(port);
                switch (tcb_queue_state)
                {
                case IPC_ENDPOINT_STATE_EMPTY:
                case IPC_ENDPOINT_STATE_SEND:
                        break;
                case IPC_ENDPOINT_STATE_RECV:
                        break;
                default:
                        /*should not achieve here*/
                        arch_cpu_relax();
                        continue;
                }
        }
}
Message_t* recv_msg(Message_Port_t* port)
{
        /*TODO*/
        while(1){
                u16 tcb_queue_state = ipc_get_queue_state(port);
                switch (tcb_queue_state)
                {
                case IPC_ENDPOINT_STATE_EMPTY:
                case IPC_ENDPOINT_STATE_RECV:
                        break;
                case IPC_ENDPOINT_STATE_SEND:
                        break;
                default:
                        /*should not achieve here*/
                        arch_cpu_relax();
                        continue;
                }
        }
}
void send_msg_async(Message_Port_t* port, Message_t* message);
void send_msg_async_broadcast(Message_Port_t* port, Message_t* message);
Message_t* recv_msg_await(Message_Port_t* port);