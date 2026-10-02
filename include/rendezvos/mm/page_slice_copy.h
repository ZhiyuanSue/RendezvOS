#ifndef _RENDEZVOS_PAGE_SLICE_COPY_H_
#define _RENDEZVOS_PAGE_SLICE_COPY_H_

/*
 * Composed copy operations on top of page_slice primitives (lookup / insert).
 * May include VSpace and map_handler; keep page_slice.c radix-only.
 */

#include <common/types.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/page_slice.h>

struct VSpace;

/**
 * @brief Copy a mapped slice range into a contiguous kernel buffer.
 *
 * @param slice    Source slice.
 * @param byte_off Offset within the logical byte stream.
 * @param dst      Kernel destination buffer (must cover @p len ).
 * @param len      Bytes to copy; 0 succeeds immediately.
 *
 * @retval REND_SUCCESS   Copied.
 * @retval -E_IN_PARAM    Bad args or range past @c slice->size.
 * @retval -E_RENDEZVOS   A pgoff in the range is not VALID (holes fail; no
 *                        zero-fill).
 */
error_t page_slice_copy_to_buffer(struct page_slice* slice, u64 byte_off,
                                  void* dst, size_t len);

/**
 * @brief Copy between two mapped slice ranges (sparse → sparse).
 *
 * Destination pgoffs must already be bound; core does not insert dst pages.
 * Rejects dangerous self-overlap on the same slice.
 *
 * @retval REND_SUCCESS   Copied.
 * @retval -E_IN_PARAM    Bad args, out-of-range, or same-slice overlap.
 * @retval -E_RENDEZVOS   Unmapped hole on src or dst.
 */
error_t page_slice_copy_to_slice(struct page_slice* dst, u64 dst_byte_off,
                                 struct page_slice* src, u64 src_byte_off,
                                 size_t len);

/**
 * @brief Deep-clone a slice's mapped radix tree (bound pgoffs only).
 *
 * Creates a new slice with the same logical @c slice->size as @p src and copies
 * only pgoffs that are valid in @p src (new owned pages via kallocator).
 * Unmapped holes in @p src are not materialized in the destination tree.
 * Clears @c PAGE_SLICE_FLAG_PIN on cloned leaves (new pages are owned).
 *
 * Does **not** copy @c append_page_slice_info; upper layers that attach typed
 * metadata must install it via their own copy hook (same discipline as task
 * append_hooks->copy vs blind memcpy).
 *
 * @param dst_out Output; set to the new slice on success.
 * @param src     Source slice.
 *
 * @retval REND_SUCCESS   Clone complete.
 * @retval -E_IN_PARAM    Bad args.
 * @retval -E_REND_NO_MEM Allocation failed.
 * @retval -E_RENDEZVOS   Insert or internal walk failure.
 *
 * @note This is memcpy deep copy, **not** COW / shared pages.
 */
error_t page_slice_clone(struct page_slice** dst_out, struct page_slice* src);

/**
 * @brief Copy a mapped slice range into a user virtual address range.
 *
 * Requires every source pgoff in range to be VALID; then copies via
 * @ref map_handler_user_kernel_copy into @p vs at @p user_va.
 *
 * @param vs             Target user address space.
 * @param user_va        Destination user VA.
 * @param slice          Source slice.
 * @param file_byte_off  Offset in the slice byte stream.
 * @param len            Bytes to copy.
 */
error_t page_slice_copy_to_user(struct VSpace* vs, u64 user_va,
                                struct page_slice* slice, u64 file_byte_off,
                                size_t len);

#endif /* _RENDEZVOS_PAGE_SLICE_COPY_H_ */
