#ifndef _RENDEZVOS_KMSG_H_
#define _RENDEZVOS_KMSG_H_

#include <common/stdarg.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/ipc/message.h>
#include <common/mm.h>

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

/*
 * Upper bound for kmsg TLV payload length. This prevents accidental large
 * allocations from malformed format strings or unexpectedly long strings.
 */
#define KMSG_MAX_PAYLOAD PAGE_SIZE

#define KMSG_MOD_CORE 1u

#define KMSG_OP_CORE_THREAD_REAP 1u
/* Power control (handled by powerd). */
#define KMSG_OP_CORE_POWER_SHUTDOWN 2u
#define KMSG_OP_CORE_POWER_REBOOT   3u

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

/**
 * @brief Build a kmsg header plus ipc_serial TLV payload and wrap it in
 * Msg_Data.
 * @param module Value stored in kmsg_hdr.module (typically port service_id).
 * @param opcode Operation code for the receiver to dispatch on.
 * @param fmt Format string for variadic arguments: one type char per argument
 *        (whitespace ignored). Supported tags: @c p (pointer), @c q (i64), @c i
 *        (i32), @c u (u32), @c s (C string, wire includes trailing NUL), @c t
 *        (port name, same wire as @c s). Each argument is encoded as
 *        type_tag + u32 len + value bytes; see ipc_serial.h.
 * @param ... Arguments matching fmt in order.
 * @return Msg_Data tagged MSG_DATA_TAG_KMSG with refcount 1, or NULL on encode
 *         error, oversize payload, or allocation failure.
 */
Msg_Data_t* kmsg_create(u16 module, u16 opcode, const char* fmt, ...);

/**
 * @brief Extract a validated kmsg view from a Message_t carrier.
 * @param msg Message whose Msg_Data must be MSG_DATA_TAG_KMSG.
 * @return Pointer to kmsg inside the message buffer, or NULL if layout or magic
 *         checks fail.
 */
const kmsg_t* kmsg_from_msg(const Message_t* msg);

#endif
