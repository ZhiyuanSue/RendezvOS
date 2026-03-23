#include <common/string.h>
#include <common/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/task/kmsg.h>
#include <rendezvos/task/tcb.h>

Msg_Data_t* kmsg_create(u16 kind, const void* payload, u32 payload_len)
{
        if (!payload && payload_len != 0)
                return NULL;
        if (payload_len > (u32)(SIZE_MAX - sizeof(kmsg_t)))
                return NULL;

        struct allocator* a = percpu(kallocator);
        if (!a)
                return NULL;

        kmsg_t* km = (kmsg_t*)a->m_alloc(a, sizeof(kmsg_t) + payload_len);
        if (!km)
                return NULL;
        memset(km, 0, sizeof(kmsg_t) + payload_len);

        km->hdr.magic = KMSG_MAGIC;
        km->hdr.version = (u16)KMSG_VERSION;
        km->hdr.kind = kind;
        km->hdr.payload_len = payload_len;
        km->hdr.src_cpu = (cpu_id_t)percpu(cpu_number);
        km->hdr.reserved_cpu = CPU_ID_INVALID;

        Thread_Base* self = get_cpu_current_thread();
        if (self) {
                km->hdr.src_tid = self->tid;
                if (self->belong_tcb)
                        km->hdr.src_pid = self->belong_tcb->pid;
                else
                        km->hdr.src_pid = INVALID_ID;
        } else {
                km->hdr.src_tid = INVALID_ID;
                km->hdr.src_pid = INVALID_ID;
        }

        if (payload_len)
                memcpy(km->payload, payload, payload_len);

        void* data = (void*)km;
        return create_message_data(RENDEZ_MSG_TYPE_KERNEL,
                                   sizeof(kmsg_t) + payload_len,
                                   &data,
                                   free_msgdata_ref_default);
}

const kmsg_t* kmsg_from_msg(const Message_t* msg)
{
        if (!msg || !msg->data)
                return NULL;
        if (msg->data->msg_type != RENDEZ_MSG_TYPE_KERNEL)
                return NULL;
        if (!msg->data->data || msg->data->data_len < sizeof(kmsg_hdr_t))
                return NULL;
        const kmsg_t* km = (const kmsg_t*)msg->data->data;
        if (km->hdr.magic != KMSG_MAGIC || km->hdr.version != KMSG_VERSION)
                return NULL;
        if (km->hdr.payload_len != (u32)(msg->data->data_len - sizeof(kmsg_t)))
                return NULL;
        return km;
}
