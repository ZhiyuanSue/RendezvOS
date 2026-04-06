#ifndef _RENDEZVOS_IPC_SERIAL_H_
#define _RENDEZVOS_IPC_SERIAL_H_

#include <common/stdarg.h>
#include <common/types.h>
#include <rendezvos/error.h>

/*
 * Compact type-length-value serialization for IPC payloads (kmsg payload uses
 * this encoding).
 *
 * Wire layout (unaligned-safe via memcpy):
 *
 *   u32 param_count
 *   repeat param_count times:
 *       u8  type_tag   (ASCII format char, see below)
 *       u32 value_len
 *       u8[value_len]  value bytes
 *
 * Format string (whitespace ignored; one char per parameter):
 *   p  void* / machine word
 *   q  i64
 *   i  i32
 *   u  u32
 *   s  char* (C string; length = strlen, no trailing NUL in wire)
 *   t  char* (same wire as s): port name registered in the global port table,
 *      used by convention as the reply endpoint for request–reply.
 *
 * Decode: for "s"/"t", the returned char* points into the message buffer; copy
 * if needed after the buffer is freed.
 *
 * High-frequency send path: ipc_serial_measure_va() then
 * ipc_serial_encode_into_va() into a caller-owned buffer (e.g. kmsg payload) —
 * one allocation, no extra serialized-buffer copy.
 *
 * Note on naming: *_va suffix means the function takes a va_list.
 */

error_t ipc_serial_measure_va(const char *fmt, va_list ap, u32 *total_out);

error_t ipc_serial_encode_into_va(void *buf, u32 total, const char *fmt,
                                  va_list ap);

void *ipc_serial_encode_va(const char *fmt, u32 *out_len, va_list ap);

void *ipc_serial_encode_alloc(const char *fmt, u32 *out_len, ...);

error_t ipc_serial_decode(const void *buf, u32 buf_len, const char *fmt, ...);

#endif
