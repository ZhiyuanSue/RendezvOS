#include <common/string.h>
#include <common/limits.h>
#include <common/stddef.h>
#include <rendezvos/ipc/ipc_serial.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>

Msg_Data_t* kmsg_create(u16 module, u16 opcode, const char* fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);

        u32 payload_len;
        if (ipc_serial_measure_va(fmt, ap, &payload_len) != REND_SUCCESS) {
                va_end(ap);
                return NULL;
        }

        const size_t km_hdr_sz = offsetof(kmsg_t, payload);
        if (payload_len > (u32)(SIZE_MAX - km_hdr_sz)) {
                va_end(ap);
                return NULL;
        }

        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator) {
                va_end(ap);
                return NULL;
        }

        kmsg_t* km = (kmsg_t*)cpu_kallocator->m_alloc(
                cpu_kallocator, km_hdr_sz + (size_t)payload_len);
        if (!km) {
                va_end(ap);
                return NULL;
        }
        memset(km, 0, km_hdr_sz + (size_t)payload_len);

        km->hdr.magic = KMSG_MAGIC;
        km->hdr.module = module;
        km->hdr.opcode = opcode;
        km->hdr.payload_len = payload_len;

        if (ipc_serial_encode_into_va(km->payload, payload_len, fmt, ap)
            != REND_SUCCESS) {
                cpu_kallocator->m_free(cpu_kallocator, km);
                va_end(ap);
                return NULL;
        }
        va_end(ap);

        void* data = (void*)km;
        return create_message_data(MSG_DATA_TAG_KMSG,
                                   (u64)(km_hdr_sz + payload_len),
                                   &data,
                                   free_msgdata_ref_default);
}

const kmsg_t* kmsg_from_msg(const Message_t* msg)
{
        if (!msg || !msg->data || msg->data->msg_type != MSG_DATA_TAG_KMSG
            || !msg->data->data
            || msg->data->data_len < offsetof(kmsg_t, payload))
                return NULL;

        const kmsg_t* km = (const kmsg_t*)msg->data->data;
        if (km->hdr.magic != KMSG_MAGIC)
                return NULL;
        if (km->hdr.payload_len
            != (u32)(msg->data->data_len - offsetof(kmsg_t, payload)))
                return NULL;
        return km;
}
