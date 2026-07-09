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
 */
error_t page_slice_copy_to_buffer(struct page_slice* slice, u64 byte_off,
                                  void* dst, size_t len);

/**
 * @brief Copy between two mapped slice ranges (sparse → sparse).
 *
 * Destination pgoffs must already be bound; core does not insert dst pages.
 */
error_t page_slice_copy_to_slice(struct page_slice* dst, u64 dst_byte_off,
                                 struct page_slice* src, u64 src_byte_off,
                                 size_t len);

/**
 * @brief Copy a mapped slice range into a user virtual address range.
 */
error_t page_slice_copy_to_user(struct VSpace* vs, u64 user_va,
                                struct page_slice* slice, u64 file_byte_off,
                                size_t len);

#endif /* _RENDEZVOS_PAGE_SLICE_COPY_H_ */
