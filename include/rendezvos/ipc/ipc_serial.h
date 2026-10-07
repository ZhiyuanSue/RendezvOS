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
 *   s  char* (C string; wire includes trailing NUL: length = strlen + 1)
 *   t  char* (same wire as s): port name registered in the global port table,
 *      used by convention as the reply endpoint for request–reply.
 *
 * Encode tag and decode fmt char must match byte-for-byte ('t' ≠ 's').
 * Empty fmt "" still yields 4-byte payload (param_count=0).
 * NULL fmt is rejected: measure / encode_into / decode return -E_IN_PARAM;
 * encode_va / encode_alloc return NULL.
 *
 * Decode: for "s"/"t", the returned char* points into the message buffer; copy
 * if needed after the buffer is freed. Decode requires off == buf_len (no
 * trailing unread bytes) — appending an extra 't' on the wire without including
 * it in fmt will fail.
 *
 * High-frequency send path: @c ipc_serial_measure_va() then
 * @c ipc_serial_encode_into_va() into a caller-owned buffer — one allocation,
 * no extra serialized-buffer copy.
 *
 * Note on naming: *_va suffix means the function takes a va_list.
 */

/**
 * @brief Compute serialized size for @p fmt / @p ap (includes 4-byte count).
 * @param fmt Format string; empty "" OK → total 4. NULL → @c -E_IN_PARAM.
 * @param ap  Variadic args matching @p fmt (copied internally; caller's list
 *            is not consumed — caller may reuse after measure).
 * @param total_out Out: byte length; must be non-NULL.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p fmt or @p total_out is NULL,
 *         or unknown format char.
 */
error_t ipc_serial_measure_va(const char *fmt, va_list ap, u32 *total_out);

/**
 * @brief Encode @p fmt / @p ap into caller-owned @p buf of length @p total.
 * @param buf   Destination; must be non-NULL and @p total >= 4.
 * @param total Exact size from a prior @c ipc_serial_measure_va (must equal
 *              the encoded length or returns @c -E_IN_PARAM).
 * @param fmt   Format string; NULL → @c -E_IN_PARAM.
 * @param ap    Args matching @p fmt.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM on NULL fmt, or bad
 *         size/tag/overflow/mismatch.
 */
error_t ipc_serial_encode_into_va(void *buf, u32 total, const char *fmt,
                                  va_list ap);

/**
 * @brief Measure + allocate + encode into a single heap buffer.
 * @param fmt     Format string (same rules as measure/encode); NULL → NULL.
 * @param out_len Out length on success; must be non-NULL.
 * @param ap      Args matching @p fmt.
 * @return Heap buffer owned by the caller (free with kallocator), or NULL on
 *         NULL fmt / measure/alloc/encode failure (@p out_len unchanged
 *         on failure).
 */
void *ipc_serial_encode_va(const char *fmt, u32 *out_len, va_list ap);

/**
 * @brief Variadic wrapper around @c ipc_serial_encode_va.
 * @param fmt     Format string; NULL → NULL.
 * @param out_len Out length on success; must be non-NULL.
 * @param ...     Args matching @p fmt.
 * @return Same as @c ipc_serial_encode_va.
 */
void *ipc_serial_encode_alloc(const char *fmt, u32 *out_len, ...);

/**
 * @brief Decode @p buf into out-parameters matching @p fmt.
 * @param buf     Serialized blob; @p buf_len must be exact (no trailing junk).
 * @param buf_len Length in bytes (>= 4).
 * @param fmt     Format string; NULL → @c -E_IN_PARAM. Param count must equal
 *                wire @c param_count.
 * @param ...     Out pointers: @c p→void**, @c q→i64*, @c i→i32*, @c u→u32*,
 *                @c s/@c t→char** (into @p buf; NULL if wire len 0).
 * @return @c REND_SUCCESS; @c -E_IN_PARAM on NULL fmt / mismatch / bad NUL /
 *         short buffer.
 */
error_t ipc_serial_decode(const void *buf, u32 buf_len, const char *fmt, ...);

#endif
