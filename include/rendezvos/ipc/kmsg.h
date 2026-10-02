#ifndef _RENDEZVOS_KMSG_H_
#define _RENDEZVOS_KMSG_H_

#include <common/stdarg.h>
#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/ipc/message.h>
#include <common/mm.h>

/**
 * @c Msg_Data.msg_type when @c Msg_Data.data points at a @c kmsg_t buffer.
 * Tags carrier layout only; routing uses @c hdr.module / @c hdr.opcode.
 */
#define MSG_DATA_TAG_KMSG 1

/**
 * Magic for the slim kmsg header (no in-band version; layout changes bump this
 * magic and all call sites together).
 * Little-endian bytes at increasing address: 'L','M','S','G'.
 */
#define KMSG_MAGIC 0x47534d4cu

/**
 * Upper bound for kmsg TLV payload length (guards against oversized alloc from
 * malformed fmt / unexpectedly long strings).
 */
#define KMSG_MAX_PAYLOAD PAGE_SIZE

/**
 * @brief Fixed header prepended to every kmsg payload.
 */
typedef struct {
        u32 magic; /* Must be @c KMSG_MAGIC.*/
        u16 module; /* Destination port @c service_id (fast-check).*/
        u16 opcode; /* Operation code for the receiver. */
        u32 payload_len; /**< Bytes of trailing TLV (@c ipc_serial).*/
} kmsg_hdr_t;

/**
 * @brief kmsg carrier: header plus flexible TLV payload array.
 */
typedef struct {
        kmsg_hdr_t hdr;
        u8 payload[]; /* Serialized TLV; length @c hdr.payload_len.*/
} kmsg_t;

/**
 * @brief Build a kmsg header plus ipc_serial TLV payload and wrap it in
 *        Msg_Data.
 *
 * Payload length must not exceed @c KMSG_MAX_PAYLOAD.
 *
 * @param module Value stored in kmsg_hdr.module (typically port service_id).
 * @param opcode Operation code for the receiver to dispatch on.
 * @param fmt Non-NULL format string (empty "" OK). Type chars: @c p @c q @c i
 *        @c u @c s @c t (see @c ipc_serial.h). Whitespace ignored.
 * @param ... Arguments matching fmt in order.
 * @return Msg_Data tagged MSG_DATA_TAG_KMSG with refcount 1, or NULL on encode
 *         error, oversize payload, or allocation failure.
 * @note Does not look up ports or fill reply fields. Routing remains by port
 *       name; @p module is a fast-check hint only.
 */
Msg_Data_t* kmsg_create(u16 module, u16 opcode, const char* fmt, ...);

/**
 * @brief Extract a validated kmsg view from a Message_t carrier.
 * @param msg Message whose Msg_Data must be MSG_DATA_TAG_KMSG.
 * @return Pointer to kmsg inside the message buffer, or NULL if layout or magic
 *         checks fail.
 * @note Validates carrier tag, buffer size, @c KMSG_MAGIC, and
 *       @c payload_len vs @c data_len. Does not validate module / opcode /
 *       TLV contents — caller uses @c ipc_serial_decode.
 */
const kmsg_t* kmsg_from_msg(const Message_t* msg);

#endif
