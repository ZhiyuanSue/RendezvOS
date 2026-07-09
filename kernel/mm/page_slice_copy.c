#include <rendezvos/mm/page_slice_copy.h>

#include <common/mm.h>
#include <common/string.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>

typedef error_t (*page_slice_copy_emit_fn)(void* ctx, vaddr src_kva,
                                           size_t chunk_len, u64 src_byte_off);

static error_t page_slice_copy_from_mapped(struct page_slice* slice,
                                           u64 byte_off, size_t len,
                                           page_slice_copy_emit_fn emit,
                                           void* ctx)
{
        size_t done = 0;
        u64 slice_size;

        if (!slice || !emit) {
                return -E_IN_PARAM;
        }
        if (len == 0) {
                return REND_SUCCESS;
        }

        slice_size = page_slice_get_size(slice);
        if (byte_off > slice_size || len > slice_size - byte_off) {
                return -E_IN_PARAM;
        }

        while (done < len) {
                u64 off = byte_off + (u64)done;
                u64 pgoff = PAGE_SLICE_BYTE_TO_PGOFF(off);
                u64 in_page = PAGE_SLICE_IN_PAGE_OFF(off);
                struct page_slice_entry* entry;
                size_t chunk;
                error_t err;

                entry = page_slice_lookup(slice, pgoff);
                if (!entry) {
                        return -E_RENDEZVOS;
                }

                chunk = PAGE_SIZE - (size_t)in_page;
                if (chunk > len - done) {
                        chunk = len - done;
                }

                err = emit(ctx,
                           entry->kernel_virtual_address + in_page,
                           chunk,
                           off);
                if (err != REND_SUCCESS) {
                        return err;
                }
                done += chunk;
        }
        return REND_SUCCESS;
}

struct page_slice_copy_to_buffer_ctx {
        u8* dst;
        size_t done;
};

static error_t page_slice_emit_to_buffer(void* ctx, vaddr src_kva,
                                         size_t chunk_len, u64 src_byte_off)
{
        struct page_slice_copy_to_buffer_ctx* out = ctx;

        (void)src_byte_off;
        memcpy(out->dst + out->done, (void*)src_kva, chunk_len);
        out->done += chunk_len;
        return REND_SUCCESS;
}

error_t page_slice_copy_to_buffer(struct page_slice* slice, u64 byte_off,
                                  void* dst, size_t len)
{
        struct page_slice_copy_to_buffer_ctx ctx = {
                .dst = dst,
                .done = 0,
        };

        if (!dst) {
                return -E_IN_PARAM;
        }
        return page_slice_copy_from_mapped(
                slice, byte_off, len, page_slice_emit_to_buffer, &ctx);
}

static bool page_slice_byte_ranges_overlap(u64 a_off, u64 b_off, size_t len)
{
        u64 a_end = a_off + (u64)len;
        u64 b_end = b_off + (u64)len;

        return a_off < b_end && b_off < a_end;
}

error_t page_slice_copy_to_slice(struct page_slice* dst, u64 dst_byte_off,
                                 struct page_slice* src, u64 src_byte_off,
                                 size_t len)
{
        size_t done = 0;
        u64 dst_size;
        u64 src_size;

        if (!dst || !src) {
                return -E_IN_PARAM;
        }
        if (len == 0) {
                return REND_SUCCESS;
        }
        if (dst == src
            && page_slice_byte_ranges_overlap(src_byte_off, dst_byte_off, len)) {
                return -E_IN_PARAM;
        }

        dst_size = page_slice_get_size(dst);
        src_size = page_slice_get_size(src);
        if (dst_byte_off > dst_size || len > dst_size - dst_byte_off) {
                return -E_IN_PARAM;
        }
        if (src_byte_off > src_size || len > src_size - src_byte_off) {
                return -E_IN_PARAM;
        }

        while (done < len) {
                u64 src_off = src_byte_off + (u64)done;
                u64 dst_off = dst_byte_off + (u64)done;
                u64 src_pgoff = PAGE_SLICE_BYTE_TO_PGOFF(src_off);
                u64 dst_pgoff = PAGE_SLICE_BYTE_TO_PGOFF(dst_off);
                u64 src_in_page = PAGE_SLICE_IN_PAGE_OFF(src_off);
                u64 dst_in_page = PAGE_SLICE_IN_PAGE_OFF(dst_off);
                struct page_slice_entry* src_entry;
                struct page_slice_entry* dst_entry;
                size_t chunk;

                src_entry = page_slice_lookup(src, src_pgoff);
                dst_entry = page_slice_lookup(dst, dst_pgoff);
                if (!src_entry || !dst_entry) {
                        return -E_RENDEZVOS;
                }

                chunk = PAGE_SIZE - (size_t)src_in_page;
                if (PAGE_SIZE - (size_t)dst_in_page < chunk) {
                        chunk = PAGE_SIZE - (size_t)dst_in_page;
                }
                if (chunk > len - done) {
                        chunk = len - done;
                }

                memcpy((void*)(dst_entry->kernel_virtual_address + dst_in_page),
                       (void*)(src_entry->kernel_virtual_address + src_in_page),
                       chunk);
                done += chunk;
        }
        return REND_SUCCESS;
}

struct page_slice_copy_to_user_ctx {
        VSpace* vs;
        u64 user_va;
        size_t done;
};

static error_t page_slice_emit_to_user(void* ctx, vaddr src_kva,
                                       size_t chunk_len, u64 src_byte_off)
{
        struct page_slice_copy_to_user_ctx* out = ctx;
        error_t err;

        (void)src_byte_off;
        err = map_handler_user_kernel_copy(
                out->vs,
                out->user_va + (u64)out->done,
                (void*)src_kva,
                chunk_len,
                true);
        if (err == REND_SUCCESS) {
                out->done += chunk_len;
        }
        return err;
}

error_t page_slice_copy_to_user(struct VSpace* vs, u64 user_va,
                                struct page_slice* slice, u64 file_byte_off,
                                size_t len)
{
        struct page_slice_copy_to_user_ctx ctx = {
                .vs = vs,
                .user_va = user_va,
                .done = 0,
        };

        if (!vs) {
                return -E_IN_PARAM;
        }

        return page_slice_copy_from_mapped(
                slice, file_byte_off, len, page_slice_emit_to_user, &ctx);
}
