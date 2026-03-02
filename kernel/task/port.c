#include <rendezvos/task/port.h>
#include <rendezvos/task/ipc.h>

Message_Port_t* create_message_port()
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_Port_t* mp =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_Port_t));

        if (mp) {
                Ipc_Request_t* dummy_requeust_node =
                        (Ipc_Request_t*)(cpu_kallocator->m_alloc(
                                cpu_kallocator, sizeof(Ipc_Request_t)));
                /* Dummy must be zeroed so free_thread_ref (via
                 * del_thread_structure) does not dereference garbage
                 * init_parameter when the dummy is dequeued and freed by
                 * msq_dequeue_check_head. */
                if (dummy_requeust_node) {
                        memset(dummy_requeust_node, 0, sizeof(Ipc_Request_t));
                        msq_init(&mp->thread_queue,
                                 &dummy_requeust_node->ms_queue_node,
                                 IPC_PORT_APPEND_BITS);
                }
        } else {
                return NULL;
        }
        return mp;
}
void delete_message_port(Message_Port_t* port)
{
        if (!port)
                return;
        /*TODO: clean the queue*/
        percpu(kallocator)->m_free(percpu(kallocator), port);
}