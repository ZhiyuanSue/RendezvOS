#ifndef _RENDEZVOS_PAGE_SLICE_H_
#define _RENDEZVOS_PAGE_SLICE_H_

/* Why we need this page slice?
 * the buddy allocator only support continuous 2M range alloc at most
 * and in kernel, the mapping is linear mapping,
 * so if the pmm allocator cannot alloc a continuous range larger then 2m
 * kernel cannot get a continuous.
 * the user part can do some frame map, but which cannot be used here
 * so we design this page slice
 * it maps a memory buffer offset to a virtual address
 * if kernel need a continuous offset ,just using this page slice.
 *
 * This is for kernel used only
 * Besides, it can be used in page cache.
 *
 * Layout (up to PAGE_SLICE_MAX_INDEX_HEIGHT index levels):
 *   [ index_L2 (9) | index_L1 (9) | index_L0 (9) | leaf_idx (7) | page_off (12)
 * ]
 *
 * Two entry kinds:
 *   leaf entry  — struct page_slice_entry in a leaf page (128 per leaf page,
 *                 pgoff map).
 *   index entry — tagged_ptr_t in an index page or slice->root (512 per index
 *                 page); packed as ptr + live + height.
 *
 * index entry (tagged_ptr) layout:
 *   [ ptr:48 (bits 0..47) | live:13 (48..60) | height:3 (61..63) ]
 *
 * height on an index entry is this entry's height, NOT the height of the page
 * ptr points to:
 *   - 0 empty;(this height only exit at slice->root)
 *   - 1 direct leaf root (slice->root.ptr is the leaf page;no index parent);
 *   - 2 bottom index page (each index entry.ptr is a leaf page);
 *   - 3..4 taller index pages (each index entry.ptr is an index page below).
 * All entries in one page(index/leaf) share the same height(logically, although
 * leaf entry have no height).
 *
 * What ptr points to follows current entry's height:
 *   height 1 — leaf page (root only);
 *   height 2 — points to leaf page;(and root ptr height is 2, points to level 2
 *              index page now)
 *   height 3..4 — points to index page.
 *
 * live counts the in-use entry of the page ptr points to.
 *   height 1 (direct leaf root only) or 2 (leaf-page index entry) —
 *     in-use leaf pgoffs in that leaf page (0..PAGE_SLICE_LEAF_CAPACITY);
 *   height 2 on slice->root when root is an index page, or height 3..4 on
 *     index entries pointing at index pages — non-empty child slots in that
 *     index page (0..PAGE_SLICE_INDEX_CAPACITY);
 *   height 0 (ptr==0) — live==0 (root only).
 *
 * slice->mapped_entries: upper-layer stat only (pgoffs with PAGE_SLICE_FLAG_VALID).
 * Updated on insert/remove; not used by radix grow/shrink/free.
 */

#include <common/types.h>
#include <common/mm.h>
#include <common/stddef.h>
#include <common/taggedptr.h>
#include <common/dsa/list.h>
#include <rendezvos/error.h>
#include <rendezvos/sync/cas_lock.h>

/* === leaf entry (pgoff mapping) === */

struct page_slice_entry {
        vaddr kernel_virtual_address;
#define PAGE_SLICE_FLAG_VALID (1ULL << 0)
        /* this entry is valid */
#define PAGE_SLICE_FLAG_PIN (1ULL << 1)
        /* pinned page, not mmu pin, just skip m_free on destroy/remove */
        u64 flags;
        union {
                struct list_entry page_list_node;
                /* it might be used for upper layer */
                u64 padding[2];
        };
};

/* === index entry (radix link: ptr + live + height) === */

typedef tagged_ptr_t page_slice_index_entry_t;

#define PAGE_SLICE_ENTRY_SIZE sizeof(struct page_slice_entry) /* expect 32 */
#define PAGE_SLICE_INDEX_ENTRY_SIZE \
        sizeof(page_slice_index_entry_t) /* expect 8 */
#define PAGE_SLICE_LEAF_CAPACITY \
        (PAGE_SIZE / PAGE_SLICE_ENTRY_SIZE) /* expect 128 */
#define PAGE_SLICE_INDEX_CAPACITY \
        (PAGE_SIZE / PAGE_SLICE_INDEX_ENTRY_SIZE) /* expect 512 */
#define PAGE_SLICE_LEAF_SHIFT       7u
#define PAGE_SLICE_INDEX_SHIFT      9u
#define PAGE_SLICE_MAX_INDEX_HEIGHT 3u
#define PAGE_SLICE_MAX_PGOFF_SHIFT \
        (PAGE_SLICE_LEAF_SHIFT     \
         + PAGE_SLICE_MAX_INDEX_HEIGHT * PAGE_SLICE_INDEX_SHIFT)
#define PAGE_SLICE_MAX_BYTE_SIZE \
        ((1ULL << PAGE_SLICE_MAX_PGOFF_SHIFT) * PAGE_SIZE)

#define PAGE_SLICE_IN_PAGE_OFF(byte_off)   ((byte_off) & (PAGE_SIZE - 1ULL))
#define PAGE_SLICE_BYTE_TO_PGOFF(byte_off) ((byte_off) / PAGE_SIZE)
#define PAGE_SLICE_SIZE_TO_PAGE_COUNT(size) \
        ((size) == 0 ? 0ULL : (((size) + PAGE_SIZE - 1ULL) / PAGE_SIZE))

#define PAGE_SLICE_LEAF_IDX(pgoff) \
        ((u64)((pgoff) & (PAGE_SLICE_LEAF_CAPACITY - 1ULL)))

#define PAGE_SLICE_INDEX_ENTRY(pgoff, level)                       \
        ((u64)(((pgoff) >> (PAGE_SLICE_LEAF_SHIFT                  \
                            + (PAGE_SLICE_INDEX_SHIFT * (level)))) \
               & (PAGE_SLICE_INDEX_CAPACITY - 1ULL)))

#define PS_ENTRY_LIVE_BITS    13u
#define PS_ENTRY_HEIGHT_BITS  3u
#define PS_ENTRY_LIVE_MASK    ((1u << PS_ENTRY_LIVE_BITS) - 1u)
#define PS_ENTRY_HEIGHT_MASK  ((1u << PS_ENTRY_HEIGHT_BITS) - 1u)
#define PS_ENTRY_HEIGHT_SHIFT PS_ENTRY_LIVE_BITS

#define PS_HEIGHT_EMPTY  0u
#define PS_HEIGHT_LEAF   1u
#define PS_HEIGHT_INDEX1 2u
#define PS_HEIGHT_INDEX2 3u
#define PS_HEIGHT_INDEX3 4u

static inline u16 ps_entry_make_tag(u8 height, u16 live)
{
        return (u16)(((height & PS_ENTRY_HEIGHT_MASK) << PS_ENTRY_HEIGHT_SHIFT)
                     | (live & PS_ENTRY_LIVE_MASK));
}

static inline u8 ps_entry_get_height(page_slice_index_entry_t entry)
{
        return (u8)((tp_get_tag(entry) >> PS_ENTRY_HEIGHT_SHIFT)
                    & PS_ENTRY_HEIGHT_MASK);
}

static inline u16 ps_entry_get_live(page_slice_index_entry_t entry)
{
        return (u16)(tp_get_tag(entry) & PS_ENTRY_LIVE_MASK);
}

static inline page_slice_index_entry_t ps_entry_new(void* ptr, u8 height,
                                                    u16 live)
{
        return tp_new(ptr, ps_entry_make_tag(height, live));
}

/* entry height 2: ptr from that index page is a leaf page. */
static inline bool ps_entry_points_to_leaf(u8 entry_height)
{
        return entry_height == PS_HEIGHT_INDEX1;
}

/* entry height 3..4: ptr from that index page is an index page. */
static inline bool ps_entry_points_to_index(u8 entry_height)
{
        return entry_height == PS_HEIGHT_INDEX2
               || entry_height == PS_HEIGHT_INDEX3;
}

static inline void* ps_entry_get_ptr(page_slice_index_entry_t entry)
{
        return tp_get_ptr(entry);
}

static inline bool ps_entry_is_none(page_slice_index_entry_t entry)
{
        return tp_is_none(entry);
}

static inline void ps_entry_set_ptr(page_slice_index_entry_t* entry, void* ptr)
{
        tp_set_ptr(entry, ptr);
}

static inline void ps_entry_set_live(page_slice_index_entry_t* entry, u16 live)
{
        u16 tag = tp_get_tag(*entry);

        tag = (u16)((tag & ~PS_ENTRY_LIVE_MASK) | (live & PS_ENTRY_LIVE_MASK));
        tp_set_tag(entry, tag);
}

static inline void ps_entry_set_height(page_slice_index_entry_t* entry,
                                       u8 height)
{
        u16 tag = tp_get_tag(*entry);

        tag = (u16)((tag & PS_ENTRY_LIVE_MASK)
                    | ((height & PS_ENTRY_HEIGHT_MASK)
                       << PS_ENTRY_HEIGHT_SHIFT));
        tp_set_tag(entry, tag);
}

/* === page_slice container === */

struct page_slice {
        page_slice_index_entry_t root;
        u64 size;
        u64 mapped_entries; /* stat for upper layer; not used in radix ops */
        cas_lock_t lock;
        u8 append_page_slice_info[];
};

static inline u8 page_slice_stored_height(const struct page_slice* slice)
{
        if (!slice || ps_entry_is_none(slice->root))
                return PS_HEIGHT_EMPTY;
        return ps_entry_get_height(slice->root);
}

static inline bool page_slice_root_empty(const struct page_slice* slice)
{
        return !slice || ps_entry_is_none(slice->root)
               || page_slice_stored_height(slice) == PS_HEIGHT_EMPTY;
}

static inline bool page_slice_root_is_leaf(const struct page_slice* slice)
{
        return slice && page_slice_stored_height(slice) == PS_HEIGHT_LEAF;
}

static inline bool page_slice_root_is_index(const struct page_slice* slice)
{
        u8 height;

        if (!slice || page_slice_root_empty(slice))
                return false;
        height = page_slice_stored_height(slice);
        return height >= PS_HEIGHT_INDEX1 && height <= PS_HEIGHT_INDEX3;
}

static inline struct page_slice_entry*
page_slice_root_get_leaf(const struct page_slice* slice)
{
        if (!page_slice_root_is_leaf(slice))
                return NULL;
        return (struct page_slice_entry*)ps_entry_get_ptr(slice->root);
}

static inline page_slice_index_entry_t*
page_slice_root_get_index(const struct page_slice* slice)
{
        if (!page_slice_root_is_index(slice))
                return NULL;
        return (page_slice_index_entry_t*)ps_entry_get_ptr(slice->root);
}

static inline u16 page_slice_root_live(const struct page_slice* slice)
{
        if (!slice || page_slice_root_empty(slice))
                return 0;
        return ps_entry_get_live(slice->root);
}

static inline void page_slice_root_clear(struct page_slice* slice)
{
        slice->root = tp_new_none();
}

static inline bool page_slice_index_entry_empty(page_slice_index_entry_t entry)
{
        return ps_entry_is_none(entry);
}

static inline void*
page_slice_index_entry_get_ptr(page_slice_index_entry_t entry)
{
        if (ps_entry_is_none(entry))
                return NULL;
        return ps_entry_get_ptr(entry);
}

static inline u16 page_slice_index_entry_live(page_slice_index_entry_t entry)
{
        if (ps_entry_is_none(entry))
                return 0;
        return ps_entry_get_live(entry);
}

static inline u8 page_slice_index_entry_height(page_slice_index_entry_t entry)
{
        if (ps_entry_is_none(entry))
                return PS_HEIGHT_EMPTY;
        return ps_entry_get_height(entry);
}

static inline bool
page_slice_index_entry_points_to_leaf(page_slice_index_entry_t entry)
{
        return ps_entry_points_to_leaf(page_slice_index_entry_height(entry));
}

static inline bool
page_slice_index_entry_points_to_index(page_slice_index_entry_t entry)
{
        return ps_entry_points_to_index(page_slice_index_entry_height(entry));
}

static inline u64 page_slice_page_count(const struct page_slice* slice)
{
        if (!slice)
                return 0;
        return PAGE_SLICE_SIZE_TO_PAGE_COUNT(slice->size);
}

/* === page slice interface === */

/**
 * @brief Read the logical byte length of a slice.
 *
 * @param slice Target slice.
 *
 * @return slice->size, or 0 if slice is NULL.
 *
 * @note Acquires slice->lock internally.
 */
u64 page_slice_get_size(struct page_slice* slice);

/**
 * @brief Set the logical byte length (grow, shrink, or destroy).
 *
 * @param slice    Address of the slice pointer.
 * @param new_size New byte length in [0, PAGE_SLICE_MAX_BYTE_SIZE].
 *
 * @retval REND_SUCCESS       new_size installed.
 * @retval -E_IN_PARAM        slice or *slice is NULL.
 * @retval -E_REND_OVERFLOW   new_size exceeds PAGE_SLICE_MAX_BYTE_SIZE.
 *
 * @par new_size == 0
 * Equivalent to page_slice_destroy: frees the radix tree, owned pages, and
 * the slice header, then sets *slice to NULL.
 *
 * @par new_size &gt; current size
 * Grows the logical range only; root height rises lazily on
 * page_slice_insert_page when a pgoff needs a taller fixed radix.
 *
 * @par new_size &lt; current size
 * Drops pages at or beyond the new page boundary, then lowers root height
 * when the root index page is zero-path only (slice->root slot live == 1 and
 * root index entry[0] is the sole occupied slot).
 *
 * @note Acquires slice->lock internally (except new_size 0, which
 *       delegates to page_slice_destroy).
 */
error_t page_slice_set_size(struct page_slice** slice, u64 new_size);

/**
 * @brief Allocate an empty page slice (no radix root yet).
 *
 * @param append_info_size Bytes after page_slice for caller-specific
 *        metadata (FAM append_page_slice_info); may be 0.
 * @param slice_size       Logical byte length; must be &gt; 0 and
 *        &lt;= PAGE_SLICE_MAX_BYTE_SIZE. pgoff must stay below
 *        page_slice_page_count.
 *
 * @return New slice, or NULL if slice_size is 0, out of range, or allocation
 *         fails.
 *
 * @note Uses per-CPU kallocator. Does not populate any pages.
 */
struct page_slice* page_slice_create(size_t append_info_size,
                                     size_t slice_size);

/**
 * @brief Tear down an entire slice: radix tree, owned pages, and header.
 *
 * @param slice Address of the slice pointer; set to NULL on success.
 *
 * @retval REND_SUCCESS   Tree freed; *slice cleared.
 * @retval -E_IN_PARAM    slice or *slice is NULL.
 *
 * @note Recursively frees index/leaf pages. Calls m_free on each leaf entry kva
 *       unless PAGE_SLICE_FLAG_PIN is set.
 *
 * @note Acquires slice->lock for the duration of the walk.
 */
error_t page_slice_destroy(struct page_slice** slice);

/**
 * @brief Look up the leaf entry for one file page index (read-only; no grow).
 *
 * @param slice Target slice.
 * @param pgoff File page index (0 = start of byte stream).
 *
 * @return Pointer to the leaf entry when bound (PAGE_SLICE_FLAG_VALID and
 *         non-zero kva), or NULL if slice is NULL, pgoff is out of range,
 *         the path is missing, or the leaf entry is not valid.
 *
 * @note Acquires slice->lock internally. For kernel access use
 *       entry->kernel_virtual_address (+ PAGE_SLICE_IN_PAGE_OFF for
 *       byte offsets); no copy is performed.
 */
struct page_slice_entry* page_slice_lookup(struct page_slice* slice, u64 pgoff);

/**
 * @brief Bind one kernel page at pgoff (lazy leaf bind; may raise root height).
 *
 * @param slice Target slice.
 * @param pgoff File page index; must be &lt; page_slice_page_count.
 * @param kva   Kernel VA of the page (must be non-zero).
 * @param flags Caller flags (PAGE_SLICE_FLAG_PIN, etc.); PAGE_SLICE_FLAG_VALID
 *        is always set by this function on success.
 *
 * @retval REND_SUCCESS    Mapping installed or already present with same kva.
 * @retval -E_IN_PARAM     Bad slice, kva, or pgoff out of range.
 * @retval -E_REND_AGAIN   Leaf entry already valid with a different kva.
 * @retval -E_REND_OVERFLOW Tree depth or allocation limit exceeded.
 * @retval -E_RENDEZVOS    Internal path conflict.
 *
 * @note Maintains index entry live on first bind and when allocating child
 *       pages (cap from entry height: leaf page → PAGE_SLICE_LEAF_CAPACITY,
 *       index page → PAGE_SLICE_INDEX_CAPACITY). Increments mapped_entries on
 *       first bind (stat only).
 */
error_t page_slice_insert_page(struct page_slice* slice, u64 pgoff, vaddr kva,
                               u64 flags);

/**
 * @brief Remove one bound pgoff (swap/evict path).
 *
 * @param slice Target slice.
 * @param pgoff File page index to detach.
 *
 * @retval REND_SUCCESS     Leaf entry cleared; owned kva freed unless
 *                         PAGE_SLICE_FLAG_PIN.
 * @retval -E_IN_PARAM      Bad slice or pgoff out of range.
 * @retval -E_REND_NOFOUND  Missing path or leaf entry not valid.
 *
 * @note Clears mapping immediately. Decrements leaf index entry live and
 *       mapped_entries (stat only). When the leaf page empties, frees it,
 *       cascades up the descend path while structural slot live hits zero,
 *       then shrink may unwrap zero-path height (root slot live == 1,
 *       entry[0] only).
 */
error_t page_slice_remove_page(struct page_slice* slice, u64 pgoff);

#endif
