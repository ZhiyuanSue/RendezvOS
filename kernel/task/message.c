#include <rendezvos/task/message.h>
#include <rendezvos/task/ebr.h>
Msg_Data_t* create_message_data(i64 msg_type, u64 data_len, void** data_ptr,
                                error_t (*free_data)(ref_count_t*))
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Msg_Data_t* msg_data =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Msg_Data_t));
        if (msg_data) {
                memset(msg_data, 0, sizeof(Msg_Data_t));
                /*now the thread that create this msg data hold it*/
                ref_init(&msg_data->refcount);
                if (data_len > 0 && data_ptr && *data_ptr) {
                        msg_data->data_len = data_len;
                        msg_data->msg_type = msg_type;
                        msg_data->data = *data_ptr;
                        msg_data->free_data = free_data;
                        /*after put msg data into the msg data structure,
                        the owner of this memory is msg structure*/
                        *data_ptr = NULL;
                } else {
                        /*we have to clean the msg structure*/
                        ref_put(&msg_data->refcount, free_data);
                        return NULL;
                }
        }
        return msg_data;
}
void delete_msgdata_structure(Msg_Data_t* msg_data)
{
        if (!msg_data)
                return;
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, (void*)msg_data);
}
error_t free_msgdata_ref_default(ref_count_t* msgdata_refcount)
{
        if (!msgdata_refcount)
                return -E_IN_PARAM;
        Msg_Data_t* msg_data =
                container_of(msgdata_refcount, Msg_Data_t, refcount);
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, msg_data->data);
        delete_msgdata_structure(msg_data);
        return REND_SUCCESS;
}

Message_t* create_message_with_msg(Msg_Data_t* msgdata)
{
        /*
        if you want to create a message,
        first, create_message_data, the refcount of msgdata: 0 -> 1
        means the thread get the msgdata(this function return a ptr to caller)
        second,create message ,the refcount of msgdata: 1 -> 2
        means the thread and the message_t get the refcount
        and the msg' refcount is init to 1, which will return to the caller.

        if you need to delete the caller's ptr to msgdata after a msg have
        created, please put one refcount.

        if you want to create a message from an exist message
        now the old msg refcount is 1, and old msgdata refcount is 1
        new msg create, will add the msgdata refcount to 2.
        */
        if (!msgdata || !ref_get_not_zero(&msgdata->refcount))
                return NULL;
        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_t* msg =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_t));
        if (msg) {
                memset(msg, 0, sizeof(Message_t));
                atomic64_store((volatile u64*)&(msg->data), (u64)msgdata);
                ref_init(&msg->ms_queue_node.refcount);
        }
        return msg;
}

Message_t* create_message_structure(void)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_t* msg =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_t));
        if (msg) {
                memset(msg, 0, sizeof(Message_t));
                ref_init(&msg->ms_queue_node.refcount);
        }
        return msg;
}
error_t fill_message_data(Message_t* msg, Msg_Data_t* msgdata)
{
        if (!msg || !msgdata || !ref_get_not_zero(&msgdata->refcount)) {
                return -E_IN_PARAM;
        }
        atomic64_store((volatile u64*)&(msg->data), (u64)msgdata);
        return REND_SUCCESS;
}
void delete_message_structure(Message_t* msg)
{
        if (!msg)
                return;
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, (void*)msg);
}
static error_t free_message_ref_real(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return -E_IN_PARAM;
        ms_queue_node_t* node =
                container_of(ref_count_ptr, ms_queue_node_t, refcount);
        Message_t* dummy_msg = container_of(node, Message_t, ms_queue_node);
        if (dummy_msg->data) {
                ref_put(&dummy_msg->data->refcount, dummy_msg->data->free_data);
        }
        delete_message_structure(dummy_msg);
        return REND_SUCCESS;
}

error_t free_message_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return -E_IN_PARAM;
        /*
         * Do not free queue nodes immediately from a lock-free path.
         * Defer reclaim until EBR says no active reader can still hold
         * transient pointers to this node.
         */
        return ebr_retire_ref(ref_count_ptr, free_message_ref_real);
}