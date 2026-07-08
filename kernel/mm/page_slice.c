#include <rendezvos/mm/page_slice.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/smp/percpu.h>

#define PS_STRUCTURAL_PATH_DEPTH 3u

static page_slice_index_entry_t*
ps_entry_as_index_page(page_slice_index_entry_t entry)
{
        if (page_slice_index_entry_empty(entry)
            || !page_slice_index_entry_points_to_index(entry))
                return NULL;
        return (page_slice_index_entry_t*)ps_entry_get_ptr(entry);
}

static struct page_slice_entry*
ps_entry_as_leaf_page(page_slice_index_entry_t entry)
{
        if (page_slice_index_entry_empty(entry)
            || !page_slice_index_entry_points_to_leaf(entry))
                return NULL;
        return (struct page_slice_entry*)ps_entry_get_ptr(entry);
}

static page_slice_index_entry_t* ps_leaf_index_entry(struct page_slice* slice,
                                                     u64 pgoff)
{
        page_slice_index_entry_t* index_page;
        u8 page_h;
        u32 level;
        u32 slot_idx;
        u32 slot_l0;

        if (page_slice_root_is_leaf(slice))
                return &slice->root;
        if (!page_slice_root_is_index(slice))
                return NULL;

        slot_l0 = PAGE_SLICE_INDEX_ENTRY(pgoff, 0);
        index_page = page_slice_root_get_index(slice);
        if (!index_page)
                return NULL;
        page_h = page_slice_stored_height(slice);
        for (level = page_h - 2; level >= 1; level--) {
                slot_idx = PAGE_SLICE_INDEX_ENTRY(pgoff, level);
                index_page = ps_entry_as_index_page(index_page[slot_idx]);
                if (!index_page)
                        return NULL;
        }
        return &index_page[slot_l0];
}

/* --- validation --- */

static error_t ps_byte_size_valid(u64 size)
{
        if (size > PAGE_SLICE_MAX_BYTE_SIZE)
                return -E_REND_OVERFLOW;
        return REND_SUCCESS;
}
static error_t ps_pgoff_valid(const struct page_slice* slice, u64 pgoff)
{
        if (!slice)
                return -E_IN_PARAM;
        if (pgoff >= page_slice_page_count(slice))
                return -E_IN_PARAM;
        return REND_SUCCESS;
}

static u8 ps_required_height_for_pgoff(u64 pgoff)
{
        if (pgoff < PAGE_SLICE_LEAF_CAPACITY)
                return PS_HEIGHT_LEAF;
        if (PAGE_SLICE_INDEX_ENTRY(pgoff, 2))
                return PS_HEIGHT_INDEX3;
        if (PAGE_SLICE_INDEX_ENTRY(pgoff, 1))
                return PS_HEIGHT_INDEX2;
        return PS_HEIGHT_INDEX1;
}

/* --- page allocation --- */

static struct page_slice_entry* ps_alloc_leaf_page(struct allocator* alloc)
{
        struct page_slice_entry* leaf;
        u32 entry_idx;

        leaf = alloc->m_alloc(alloc, PAGE_SIZE);
        if (!leaf)
                return NULL;
        for (entry_idx = 0; entry_idx < PAGE_SLICE_LEAF_CAPACITY; entry_idx++)
                INIT_LIST_HEAD(&leaf[entry_idx].page_list_node);
        return leaf;
}

/* --- live counters (2×height table → live cap) --- */

/*
 * live max by (is slice->root, height):
 *
 *              h:  0    1    2    3    4
 *   non-root:      0  128  128  512  512
 *   root:          0  128  512  512  512
 */
static const u16 ps_entry_live_max_table[2][PS_HEIGHT_INDEX3 + 1u] = {
        {0,
         (u16)PAGE_SLICE_LEAF_CAPACITY,
         (u16)PAGE_SLICE_LEAF_CAPACITY,
         (u16)PAGE_SLICE_INDEX_CAPACITY,
         (u16)PAGE_SLICE_INDEX_CAPACITY},
        {0,
         (u16)PAGE_SLICE_LEAF_CAPACITY,
         (u16)PAGE_SLICE_INDEX_CAPACITY,
         (u16)PAGE_SLICE_INDEX_CAPACITY,
         (u16)PAGE_SLICE_INDEX_CAPACITY},
};

static void ps_entry_live_inc(const struct page_slice* slice,
                              page_slice_index_entry_t* entry)
{
        u16 live;

        if (!entry)
                return;
        live = ps_entry_get_live(*entry);
        if (live < ps_entry_live_max_table[(slice && entry == &slice->root)]
                                          [ps_entry_get_height(*entry)])
                ps_entry_set_live(entry, (u16)(live + 1));
}

static void ps_entry_live_dec(page_slice_index_entry_t* entry)
{
        u16 live;

        if (!entry)
                return;
        live = ps_entry_get_live(*entry);
        if (live > 0)
                ps_entry_set_live(entry, (u16)(live - 1));
}

/* --- grow --- */

static error_t ps_raise_height(struct page_slice* slice, u8 target_height,
                               struct allocator* alloc)
{
        page_slice_index_entry_t* new_root;
        void* child = NULL;
        u8 cur_height;
        u8 new_height;
        u16 wrapped_live;

        if (target_height < PS_HEIGHT_INDEX1)
                return REND_SUCCESS;
        if (target_height > PS_HEIGHT_INDEX3)
                return -E_REND_OVERFLOW;

        while (page_slice_stored_height(slice) < target_height) {
                cur_height = page_slice_stored_height(slice);
                new_height = cur_height == PS_HEIGHT_EMPTY ?
                                     PS_HEIGHT_INDEX1 :
                                     (u8)(cur_height + 1);
                new_root = alloc->m_alloc(alloc, PAGE_SIZE);
                if (!new_root)
                        return -E_REND_NO_MEM;

                wrapped_live = 0;
                if (page_slice_root_is_leaf(slice)) {
                        child = (void*)page_slice_root_get_leaf(slice);
                } else if (page_slice_root_is_index(slice)) {
                        child = (void*)page_slice_root_get_index(slice);
                }
                wrapped_live = page_slice_root_live(slice);
                /* Empty slots remain tp_new_none(); never ps_entry_new(NULL,…). */
                if (child)
                        new_root[0] = ps_entry_new(child, new_height,
                                                    wrapped_live);

                slice->root = ps_entry_new(
                        new_root,
                        new_height,
                        ps_entry_is_none(new_root[0]) ? 0 : 1);
        }
        return REND_SUCCESS;
}

static error_t ps_descend_pgoff(struct page_slice* slice, u64 pgoff,
                                bool create, struct allocator* alloc,
                                struct page_slice_entry** out_entry,
                                page_slice_index_entry_t** path0_out,
                                page_slice_index_entry_t** path1_out,
                                page_slice_index_entry_t** path2_out)
{
        page_slice_index_entry_t* index_page;
        page_slice_index_entry_t* bottom_index;
        page_slice_index_entry_t* structural_parent;
        page_slice_index_entry_t* child_index;
        page_slice_index_entry_t leaf_index;
        struct page_slice_entry* leaf;
        page_slice_index_entry_t* path0;
        page_slice_index_entry_t* path1;
        page_slice_index_entry_t* path2;
        u8 path_len;
        u8 stored_h;
        u8 page_h;
        u32 level;
        u32 slot_idx;
        u32 slot_l0;

        slot_l0 = PAGE_SLICE_INDEX_ENTRY(pgoff, 0);
        stored_h = page_slice_stored_height(slice);
        if (!create && stored_h < ps_required_height_for_pgoff(pgoff))
                return -E_REND_NOFOUND;
        path0 = NULL;
        path1 = NULL;
        path2 = NULL;
        path_len = 0;
        bottom_index = NULL;

        if (stored_h >= PS_HEIGHT_INDEX1) {
                index_page = page_slice_root_get_index(slice);
                if (!index_page)
                        return -E_REND_NOFOUND;
                page_h = stored_h;
                structural_parent = &slice->root;
                if (path0_out) {
                        path0 = structural_parent;
                        path_len = 1;
                }

                for (level = page_h - 2; level >= 1; level--) {
                        slot_idx = PAGE_SLICE_INDEX_ENTRY(pgoff, level);
                        leaf_index = index_page[slot_idx];
                        if (page_slice_index_entry_empty(leaf_index)) {
                                if (!create)
                                        return -E_REND_NOFOUND;
                                child_index = alloc->m_alloc(alloc, PAGE_SIZE);
                                if (!child_index)
                                        return -E_REND_NO_MEM;
                                index_page[slot_idx] =
                                        ps_entry_new(child_index, page_h, 0);
                                ps_entry_live_inc(slice, structural_parent);
                        }
                        structural_parent = &index_page[slot_idx];
                        if (path0_out) {
                                if (path_len >= PS_STRUCTURAL_PATH_DEPTH)
                                        return -E_RENDEZVOS;
                                if (path_len == 1)
                                        path1 = structural_parent;
                                else
                                        path2 = structural_parent;
                                path_len++;
                        }
                        index_page =
                                ps_entry_as_index_page(index_page[slot_idx]);
                        if (!index_page)
                                return create ? -E_RENDEZVOS : -E_REND_NOFOUND;
                        page_h--;
                }
                bottom_index = index_page;
                leaf_index = bottom_index[slot_l0];
        } else {
                if (page_slice_root_is_leaf(slice)) {
                        leaf = page_slice_root_get_leaf(slice);
                        if (!leaf)
                                return create ? -E_RENDEZVOS : -E_REND_NOFOUND;
                        goto descend_done;
                }
                if (!create)
                        return -E_REND_NOFOUND;
                leaf_index = slice->root;
                structural_parent = NULL;
        }

        if (page_slice_index_entry_empty(leaf_index)) {
                if (!create)
                        return -E_REND_NOFOUND;
                leaf = ps_alloc_leaf_page(alloc);
                if (!leaf)
                        return -E_REND_NO_MEM;
                if (bottom_index) {
                        bottom_index[slot_l0] =
                                ps_entry_new(leaf, page_h, 0);
                        ps_entry_live_inc(slice, structural_parent);
                } else {
                        slice->root = ps_entry_new(leaf, PS_HEIGHT_LEAF, 0);
                }
        } else {
                leaf = ps_entry_as_leaf_page(leaf_index);
                if (!leaf)
                        return create ? -E_RENDEZVOS : -E_REND_NOFOUND;
        }

descend_done:
        if (path0_out)
                *path0_out = path0;
        if (path1_out)
                *path1_out = path1;
        if (path2_out)
                *path2_out = path2;

        *out_entry = &leaf[PAGE_SLICE_LEAF_IDX(pgoff)];
        return REND_SUCCESS;
}

static error_t ps_get_entry(struct page_slice* slice, u64 pgoff, bool create,
                            struct page_slice_entry** out_entry,
                            page_slice_index_entry_t** path0_out,
                            page_slice_index_entry_t** path1_out,
                            page_slice_index_entry_t** path2_out)
{
        struct allocator* alloc = percpu(kallocator);
        u8 need_height;
        error_t err;

        err = ps_pgoff_valid(slice, pgoff);
        if (err != REND_SUCCESS)
                return err;

        if (create) {
                need_height = ps_required_height_for_pgoff(pgoff);
                err = ps_raise_height(slice, need_height, alloc);
                if (err != REND_SUCCESS)
                        return err;
        }

        return ps_descend_pgoff(slice,
                                pgoff,
                                create,
                                alloc,
                                out_entry,
                                path0_out,
                                path1_out,
                                path2_out);
}

/* --- leaf entry / page free --- */

static void ps_clear_leaf_entry(struct allocator* alloc,
                                struct page_slice_entry* entry)
{
        vaddr page_address;
        u64 flags;

        if (!entry)
                return;
        page_address = entry->kernel_virtual_address;
        flags = entry->flags;
        entry->kernel_virtual_address = 0;
        entry->flags = 0;
        if (list_node_is_valid(&entry->page_list_node))
                list_del_init(&entry->page_list_node);
        if (page_address != 0 && !(flags & PAGE_SLICE_FLAG_PIN))
                alloc->m_free(alloc, (void*)page_address);
}

static void ps_free_leaf_page(struct page_slice_entry* leaf,
                              struct allocator* alloc)
{
        u32 entry_idx;

        if (!leaf)
                return;
        for (entry_idx = 0; entry_idx < PAGE_SLICE_LEAF_CAPACITY; entry_idx++)
                ps_clear_leaf_entry(alloc, &leaf[entry_idx]);
        alloc->m_free(alloc, leaf);
}

static void ps_free_index_page_recursive(page_slice_index_entry_t* index_page,
                                         u8 page_h, struct allocator* alloc)
{
        u32 slot_idx;
        page_slice_index_entry_t entry;

        if (!index_page)
                return;
        for (slot_idx = 0; slot_idx < PAGE_SLICE_INDEX_CAPACITY; slot_idx++) {
                entry = index_page[slot_idx];
                if (page_slice_index_entry_empty(entry))
                        continue;
                if (page_slice_index_entry_points_to_leaf(entry))
                        ps_free_leaf_page(ps_entry_as_leaf_page(entry), alloc);
                else
                        ps_free_index_page_recursive(
                                ps_entry_as_index_page(entry),
                                (u8)(page_h - 1), alloc);
        }
        alloc->m_free(alloc, index_page);
}

/* --- remove cascade --- */

static page_slice_index_entry_t*
ps_structural_path_entry(page_slice_index_entry_t* path0,
                         page_slice_index_entry_t* path1,
                         page_slice_index_entry_t* path2, u8 level)
{
        if (level == 0)
                return path0;
        if (level == 1)
                return path1;
        return path2;
}

static void ps_cascade_up_empty_index(struct allocator* alloc,
                                      page_slice_index_entry_t* path0,
                                      page_slice_index_entry_t* path1,
                                      page_slice_index_entry_t* path2,
                                      u8 path_len)
{
        page_slice_index_entry_t* index_entry;
        void* child;
        int level;

        if (path_len == 0)
                return;

        for (level = (int)path_len - 1; level >= 0; level--) {
                index_entry = ps_structural_path_entry(
                        path0, path1, path2, (u8)level);
                if (ps_entry_get_live(*index_entry) != 0)
                        break;

                child = ps_entry_get_ptr(*index_entry);
                if (child)
                        alloc->m_free(alloc, child);
                *index_entry = tp_new_none();

                if (level > 0)
                        ps_entry_live_dec(ps_structural_path_entry(
                                path0, path1, path2, (u8)(level - 1)));
        }
}

static error_t ps_remove_page_locked(struct page_slice* slice,
                                     struct allocator* alloc, u64 pgoff,
                                     bool missing_ok)
{
        page_slice_index_entry_t* path0;
        page_slice_index_entry_t* path1;
        page_slice_index_entry_t* path2;
        u8 path_len;
        struct page_slice_entry* entry;
        page_slice_index_entry_t* leaf_index;
        page_slice_index_entry_t* structural_parent;
        struct page_slice_entry* leaf;
        bool was_valid;
        error_t err;

        if (page_slice_root_empty(slice))
                return missing_ok ? REND_SUCCESS : -E_REND_NOFOUND;

        err = ps_descend_pgoff(
                slice, pgoff, false, alloc, &entry, &path0, &path1, &path2);
        if (err != REND_SUCCESS)
                return missing_ok ? REND_SUCCESS : err;

        leaf_index = ps_leaf_index_entry(slice, pgoff);
        if (!leaf_index)
                return missing_ok ? REND_SUCCESS : -E_REND_NOFOUND;

        was_valid = (entry->flags & PAGE_SLICE_FLAG_VALID)
                    && entry->kernel_virtual_address != 0;
        if (!was_valid)
                return missing_ok ? REND_SUCCESS : -E_REND_NOFOUND;

        ps_clear_leaf_entry(alloc, entry);
        if (slice->mapped_entries > 0)
                slice->mapped_entries--;

        ps_entry_live_dec(leaf_index);
        if (ps_entry_get_live(*leaf_index) != 0)
                return REND_SUCCESS;

        path_len = !path0 ? 0 : !path1 ? 1 : !path2 ? 2 : 3;
        structural_parent =
                path_len > 0 ?
                        ps_structural_path_entry(
                                path0, path1, path2, (u8)(path_len - 1)) :
                        NULL;
        if (!page_slice_index_entry_empty(*leaf_index)) {
                if (leaf_index == &slice->root
                    && page_slice_root_is_leaf(slice))
                        leaf = page_slice_root_get_leaf(slice);
                else
                        leaf = ps_entry_as_leaf_page(*leaf_index);
                *leaf_index = tp_new_none();
                if (leaf)
                        alloc->m_free(alloc, leaf);
                if (structural_parent)
                        ps_entry_live_dec(structural_parent);
        }
        ps_cascade_up_empty_index(alloc, path0, path1, path2, path_len);
        return REND_SUCCESS;
}

/* --- shrink (zero-path, live-driven) --- */

static void ps_shrink(struct page_slice* slice, struct allocator* alloc)
{
        page_slice_index_entry_t* root_index;
        page_slice_index_entry_t leaf_index;
        page_slice_index_entry_t child_index;
        struct page_slice_entry* leaf;
        u8 root_h;

        while (page_slice_root_is_index(slice)
               && page_slice_root_live(slice) == 1) {
                root_index = page_slice_root_get_index(slice);
                if (!root_index || page_slice_index_entry_empty(root_index[0]))
                        break;

                if (page_slice_stored_height(slice) == PS_HEIGHT_INDEX1) {
                        leaf_index = root_index[0];
                        leaf = ps_entry_as_leaf_page(leaf_index);
                        if (leaf) {
                                slice->root = ps_entry_new(
                                        leaf,
                                        PS_HEIGHT_LEAF,
                                        ps_entry_get_live(leaf_index));
                                alloc->m_free(alloc, root_index);
                                return;
                        }
                }

                root_h = page_slice_stored_height(slice);
                child_index = root_index[0];
                alloc->m_free(alloc, root_index);
                slice->root = ps_entry_new(ps_entry_get_ptr(child_index),
                                           (u8)(root_h - 1),
                                           ps_entry_get_live(child_index));
        }

        if (page_slice_root_is_leaf(slice)
            && page_slice_root_live(slice) == 0) {
                ps_free_leaf_page(page_slice_root_get_leaf(slice), alloc);
                page_slice_root_clear(slice);
        } else if (page_slice_root_is_index(slice)
                   && page_slice_root_live(slice) == 0) {
                ps_free_index_page_recursive(
                        page_slice_root_get_index(slice),
                        page_slice_stored_height(slice),
                        alloc);
                page_slice_root_clear(slice);
        }
}

/* --- public API --- */

struct page_slice* page_slice_create(size_t append_info_size, size_t slice_size)
{
        struct allocator* alloc = percpu(kallocator);
        struct page_slice* slice;

        if (!alloc || slice_size == 0)
                return NULL;
        if (ps_byte_size_valid(slice_size) != REND_SUCCESS)
                return NULL;
        slice = alloc->m_alloc(alloc,
                               sizeof(struct page_slice) + append_info_size);
        if (!slice)
                return NULL;
        page_slice_root_clear(slice);
        slice->size = slice_size;
        slice->mapped_entries = 0;
        lock_init_cas(&slice->lock);
        return slice;
}

u64 page_slice_get_size(struct page_slice* slice)
{
        u64 size;

        if (!slice)
                return 0;
        lock_cas(&slice->lock);
        size = slice->size;
        unlock_cas(&slice->lock);
        return size;
}

error_t page_slice_set_size(struct page_slice** slice, u64 new_size)
{
        struct page_slice* ps;
        struct allocator* alloc = percpu(kallocator);
        u64 new_pages;
        error_t err;

        if (!slice || !*slice)
                return -E_IN_PARAM;
        if (new_size == 0)
                return page_slice_destroy(slice);

        ps = *slice;
        err = ps_byte_size_valid(new_size);
        if (err != REND_SUCCESS)
                return err;

        lock_cas(&ps->lock);
        if (new_size == ps->size) {
                err = REND_SUCCESS;
                goto out;
        }
        if (new_size < ps->size) {
                u64 old_pages = page_slice_page_count(ps);
                u64 pgoff;

                new_pages = PAGE_SLICE_SIZE_TO_PAGE_COUNT(new_size);
                if (new_pages < old_pages) {
                        for (pgoff = old_pages; pgoff > new_pages; pgoff--)
                                ps_remove_page_locked(
                                        ps, alloc, pgoff - 1, true);
                }
        }
        ps->size = new_size;
        ps_shrink(ps, alloc);
        err = REND_SUCCESS;
out:
        unlock_cas(&ps->lock);
        return err;
}

error_t page_slice_destroy(struct page_slice** slice)
{
        struct allocator* alloc = percpu(kallocator);
        struct page_slice* ps;

        if (!slice || !*slice)
                return -E_IN_PARAM;
        ps = *slice;
        lock_cas(&ps->lock);
        if (!page_slice_root_empty(ps)) {
                u8 stored_h = page_slice_stored_height(ps);

                if (page_slice_root_is_leaf(ps))
                        ps_free_leaf_page(page_slice_root_get_leaf(ps), alloc);
                else
                        ps_free_index_page_recursive(
                                page_slice_root_get_index(ps), stored_h, alloc);
                page_slice_root_clear(ps);
        }
        unlock_cas(&ps->lock);
        if (alloc)
                alloc->m_free(alloc, ps);
        *slice = NULL;
        return REND_SUCCESS;
}

struct page_slice_entry* page_slice_lookup(struct page_slice* slice, u64 pgoff)
{
        struct page_slice_entry* entry = NULL;
        error_t err;

        if (!slice)
                return NULL;
        lock_cas(&slice->lock);
        err = ps_get_entry(slice, pgoff, false, &entry, NULL, NULL, NULL);
        unlock_cas(&slice->lock);
        if (err != REND_SUCCESS)
                return NULL;
        if (!(entry->flags & PAGE_SLICE_FLAG_VALID)
            || entry->kernel_virtual_address == 0)
                return NULL;
        return entry;
}

error_t page_slice_insert_page(struct page_slice* slice, u64 pgoff,
                               vaddr page_address, u64 flags)
{
        struct page_slice_entry* entry = NULL;
        page_slice_index_entry_t* leaf_index;
        bool was_valid;
        error_t err;

        if (!slice || page_address == 0)
                return -E_IN_PARAM;
        lock_cas(&slice->lock);
        err = ps_get_entry(slice, pgoff, true, &entry, NULL, NULL, NULL);
        if (err != REND_SUCCESS)
                goto out;

        was_valid = (entry->flags & PAGE_SLICE_FLAG_VALID)
                    && entry->kernel_virtual_address != 0;
        if (was_valid) {
                if (entry->kernel_virtual_address == page_address) {
                        err = REND_SUCCESS;
                        goto out;
                }
                err = -E_REND_AGAIN;
                goto out;
        }

        entry->kernel_virtual_address = page_address;
        entry->flags = flags | PAGE_SLICE_FLAG_VALID;
        if (list_node_is_detached(&entry->page_list_node))
                INIT_LIST_HEAD(&entry->page_list_node);

        slice->mapped_entries++;
        leaf_index = ps_leaf_index_entry(slice, pgoff);
        if (leaf_index)
                ps_entry_live_inc(slice, leaf_index);

        err = REND_SUCCESS;
out:
        unlock_cas(&slice->lock);
        return err;
}

error_t page_slice_remove_page(struct page_slice* slice, u64 pgoff)
{
        struct allocator* alloc = percpu(kallocator);
        error_t err;

        if (!slice)
                return -E_IN_PARAM;

        err = ps_pgoff_valid(slice, pgoff);
        if (err != REND_SUCCESS)
                return err;

        lock_cas(&slice->lock);
        err = ps_remove_page_locked(slice, alloc, pgoff, false);
        if (err == REND_SUCCESS) {
                u8 pass;

                for (pass = 0; pass < PS_HEIGHT_INDEX3; pass++)
                        ps_shrink(slice, alloc);
        }
        unlock_cas(&slice->lock);
        return err;
}
