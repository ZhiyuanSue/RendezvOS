#include <rendezvos/mm/page_slice_copy.h>

#include <common/mm.h>
#include <common/string.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>

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
        err = map_handler_user_kernel_copy(out->vs,
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

static error_t ps_clone_copy_leaf_entry(struct page_slice* dst,
                                        struct allocator* alloc, u64 pgoff,
                                        struct page_slice_entry* src_entry)
{
        vaddr new_page;
        u64 flags;
        error_t err;

        if (!(src_entry->flags & PAGE_SLICE_FLAG_VALID)
            || src_entry->kernel_virtual_address == 0)
                return REND_SUCCESS;

        new_page = (vaddr)alloc->m_alloc(alloc, PAGE_SIZE);
        if (!new_page)
                return -E_REND_NO_MEM;

        memcpy((void*)new_page,
               (void*)src_entry->kernel_virtual_address,
               PAGE_SIZE);
        flags = src_entry->flags & ~PAGE_SLICE_FLAG_PIN;
        err = page_slice_insert_page(dst, pgoff, new_page, flags);
        if (err != REND_SUCCESS) {
                alloc->m_free(alloc, (void*)new_page);
                return err;
        }
        return REND_SUCCESS;
}

#define PS_INDEX_SLOT_STRIDE(stored_h) \
        ((u64)PAGE_SLICE_LEAF_CAPACITY \
         << (PAGE_SLICE_INDEX_SHIFT * ((stored_h) - PS_HEIGHT_INDEX1)))

static error_t ps_clone_copy_leaf_page(struct page_slice* dst,
                                       struct allocator* alloc, u64 pgoff_base,
                                       struct page_slice_entry* leaf)
{
        u32 idx;

        for (idx = 0; idx < PAGE_SLICE_LEAF_CAPACITY; idx++) {
                error_t err = ps_clone_copy_leaf_entry(
                        dst, alloc, pgoff_base + idx, &leaf[idx]);
                if (err != REND_SUCCESS)
                        return err;
        }
        return REND_SUCCESS;
}

static error_t ps_clone_walk_mapped(struct page_slice* src,
                                    struct page_slice* dst,
                                    struct allocator* alloc)
{
        u8 stored_h;
        error_t err;
        int index_levels;
        int cur;
        u32 slot[PAGE_SLICE_MAX_INDEX_HEIGHT];
        page_slice_index_entry_t* index[PAGE_SLICE_MAX_INDEX_HEIGHT];
        u64 pgoff_base[PAGE_SLICE_MAX_INDEX_HEIGHT];

        if (page_slice_root_empty(src))
                return REND_SUCCESS;

        err = REND_SUCCESS;
        lock_cas(&src->lock);
        stored_h = page_slice_stored_height(src);

        if (page_slice_root_is_leaf(src)) {
                struct page_slice_entry* leaf = page_slice_root_get_leaf(src);

                if (leaf)
                        err = ps_clone_copy_leaf_page(dst, alloc, 0, leaf);
                unlock_cas(&src->lock);
                return err;
        }

        if (!page_slice_root_is_index(src) || stored_h < PS_HEIGHT_INDEX1) {
                unlock_cas(&src->lock);
                return REND_SUCCESS;
        }

        index_levels = (int)stored_h - 1;
        index[0] = page_slice_root_get_index(src);
        if (!index[0]) {
                unlock_cas(&src->lock);
                return REND_SUCCESS;
        }

        pgoff_base[0] = 0;
        memset(slot, 0, sizeof(slot));
        cur = 0;

        while (err == REND_SUCCESS) {
                if (cur < index_levels - 1) {
                        page_slice_index_entry_t entry;
                        page_slice_index_entry_t* child;

                        if (slot[cur] >= PAGE_SLICE_INDEX_CAPACITY) {
                                if (cur == 0)
                                        break;
                                slot[cur] = 0;
                                cur--;
                                slot[cur]++;
                                continue;
                        }

                        entry = index[cur][slot[cur]];
                        if (page_slice_index_entry_empty(entry)
                            || !page_slice_index_entry_points_to_index(entry)) {
                                slot[cur]++;
                                continue;
                        }

                        child = page_slice_index_entry_get_ptr(entry);
                        if (!child) {
                                slot[cur]++;
                                continue;
                        }

                        cur++;
                        index[cur] = child;
                        pgoff_base[cur] =
                                pgoff_base[cur - 1]
                                + (u64)slot[cur - 1]
                                          * PS_INDEX_SLOT_STRIDE(
                                                  stored_h - (u8)(cur - 1));
                        slot[cur] = 0;
                        continue;
                }

                if (slot[cur] >= PAGE_SLICE_INDEX_CAPACITY) {
                        if (cur == 0)
                                break;
                        slot[cur] = 0;
                        cur--;
                        slot[cur]++;
                        continue;
                }

                page_slice_index_entry_t entry = index[cur][slot[cur]];
                struct page_slice_entry* leaf;
                u64 leaf_base;

                if (!page_slice_index_entry_empty(entry)
                    && page_slice_index_entry_points_to_leaf(entry))
                        leaf = page_slice_index_entry_get_ptr(entry);
                else
                        leaf = NULL;

                if (leaf) {
                        leaf_base = pgoff_base[cur]
                                    + (u64)slot[cur]
                                              * PS_INDEX_SLOT_STRIDE(
                                                      PS_HEIGHT_INDEX1);
                        err = ps_clone_copy_leaf_page(
                                dst, alloc, leaf_base, leaf);
                }
                slot[cur]++;
        }

        unlock_cas(&src->lock);
        return err;
}

error_t page_slice_clone(struct page_slice** dst_out, struct page_slice* src)
{
        struct allocator* alloc = percpu(kallocator);
        struct page_slice* dst;
        u64 src_size;
        error_t err;

        if (!dst_out || !src || !alloc)
                return -E_IN_PARAM;

        *dst_out = NULL;
        src_size = page_slice_get_size(src);
        if (src_size == 0)
                return -E_IN_PARAM;

        dst = page_slice_create(0, (size_t)src_size);
        if (!dst)
                return -E_REND_NO_MEM;

        err = ps_clone_walk_mapped(src, dst, alloc);
        if (err != REND_SUCCESS) {
                page_slice_destroy(&dst);
                return err;
        }

        *dst_out = dst;
        return REND_SUCCESS;
}
