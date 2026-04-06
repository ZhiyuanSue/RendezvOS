#ifndef _RENDEZVOS_KMSG_H_
#define _RENDEZVOS_KMSG_H_

#include <common/stdarg.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/ipc/message.h>

/*
 * Msg_Data.msg_type value when Msg_Data.data points to a kmsg_t buffer.
 * This only tags the *carrier layout*; operation routing uses hdr.module and
 * hdr.opcode. Other Msg_Data users (tests, raw payloads) use their own tags.
 */
#define MSG_DATA_TAG_KMSG 1

/*
 * Magic for the slim kmsg header (no in-band version field; layout changes must
 * bump this magic and all call sites together).
 */
/* Little-endian bytes at increasing address: 'L','M','S','G'. */
#define KMSG_MAGIC 0x47534d4cu

#define KMSG_MOD_CORE 1u

#define KMSG_OP_CORE_THREAD_REAP 1u

typedef struct {
        u32 magic;
        u16 module;
        u16 opcode;
        u32 payload_len;
} kmsg_hdr_t;

typedef struct {
        kmsg_hdr_t hdr;
        u8 payload[];
} kmsg_t;

/*
 * Build a kmsg (serialized payload per ipc_serial.h) and wrap it in Msg_Data.
 * Public name is kmsg_create — the encoding is an implementation detail.
 */
Msg_Data_t* kmsg_create(u16 module, u16 opcode, const char* fmt, ...);
const kmsg_t* kmsg_from_msg(const Message_t* msg);

#endif
