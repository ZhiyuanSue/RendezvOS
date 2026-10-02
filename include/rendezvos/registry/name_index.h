#ifndef _RENDEZVOS_NAME_INDEX_H_
#define _RENDEZVOS_NAME_INDEX_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/spin_lock.h>

/*
 * name_index: global string-keyed index (slot array + OA hash + per-row
 * generation for cache tokens — see @c name_index_t / @c name_index_row).
 *
 * - Register/lookup by NUL-terminated name (length bounded by name_len_max).
 * - Optional refcount hooks (hold/drop) for lookup paths that return a live
 *   ref.
 * - Optional callbacks when a value is linked/unlinked from the index.
 *
 * Not a per-thread cache; callers may cache (row_index, row_gen) in
 * name_index_token_t and resolve under the index lock.
 *
 * Locking: register / register_abort / unregister / search assume the caller
 * already holds @c idx->lock. lookup / resolve / fini take that MCS lock
 * internally — do not nest another MCS @c me on the same lock on one CPU.
 */

/** Freelist head when empty (never a valid row index). */
#define NAME_INDEX_FREE_HEAD_INVALID U64_MAX
/** Invalid row index in a cache token (never a valid row index). */
#define NAME_INDEX_ROW_INDEX_INVALID ((u32) - 1)

/**
 * @brief Cacheable handle for a name_index row (gen truncated to u16).
 *
 * Invalidate with name_index_token_invalidate. used after unregister+reuse, gen
 * changes and resolve fails (long-lived u16 wrap is a theoretical alias risk).
 */
typedef struct {
        u32 row_index;
        u16 row_gen;
} name_index_token_t;

static inline void name_index_token_invalidate(name_index_token_t* tok)
{
        if (!tok)
                return;
        tok->row_index = NAME_INDEX_ROW_INDEX_INVALID;
        tok->row_gen = 0;
}

typedef const char* (*name_index_get_name_fn)(void* value);
typedef bool (*name_index_hold_fn)(void* value);
typedef void (*name_index_drop_fn)(void* value);
typedef void (*name_index_on_register_fn)(void* value, void* owner_context);
typedef void (*name_index_on_unregister_fn)(void* value, void* owner_context);

struct name_index_row {
        u64 gen;
        u64 used; /* 0 free, 1 used */
        union {
                void* value;
                u64 next_free;
        } storage;
};

typedef struct name_index {
        spin_lock lock;
        struct allocator* alloc;

        void* owner_context;
        name_index_get_name_fn get_name;
        name_index_hold_fn hold;
        name_index_drop_fn drop;
        name_index_on_register_fn on_register;
        name_index_on_unregister_fn on_unregister;

        struct name_index_row* rows;
        u64 row_cap;
        u64 free_head;
        u64 live;

        i64* ht; /* OA: empty/tomb/row index */
        u64 ht_cap;
        u64 ht_mask;
        u64 ht_tombs;

        u32 name_len_max;
} name_index_t;

/**
 * @brief Allocate initial rows/HT; install hooks. Does not take lock.
 */
void name_index_init(name_index_t* idx, struct allocator* alloc,
                     u32 name_len_max, void* owner_context,
                     name_index_get_name_fn get_name, name_index_hold_fn hold,
                     name_index_drop_fn drop,
                     name_index_on_register_fn on_register,
                     name_index_on_unregister_fn on_unregister);

/**
 * @brief Tear down under MCS lock: drop remaining values, free rows/HT.
 */
void name_index_fini(name_index_t* idx);

/**
 * @brief Find by name without hold lock (caller must hold idx->lock).
 * @param out_row_idx Optional; filled with row index if found
 * @return value pointer or NULL
 */
void* name_index_search(name_index_t* idx, const char* name, u64* out_row_idx);

/**
 * @brief Insert @p value into HT; call on_register. No lock.
 *
 * Does not check for an existing same name — callers must search then
 * register if uniqueness is required. May grow HT. @p out_reg_row_idx
 * receives the new row index.
 */
error_t name_index_register(name_index_t* idx, void* value,
                            u64* out_reg_row_idx);
/**
 * @brief Roll back a register that failed after row allocation (no lock).
 */
void name_index_register_abort(name_index_t* idx, u64 row_idx, void* value);
/**
 * @brief Remove row; on_unregister; free slot. Caller holds lock.
 */
void name_index_unregister(name_index_t* idx, void* value, u64 row_idx,
                           const char* name);

/**
 * @brief Locked lookup by name; fill @p tok_out if non-NULL.
 * @return value with a live ref, or NULL.
 *
 * If @c idx->hold is NULL: returns the indexed pointer with no extra ref
 * (caller must not assume ownership).
 * If @c idx->hold is set: a non-NULL return means @c hold(value) already
 * succeeded — caller must later @c drop / @c ref_put. If @c hold
 * fails (e.g. ref already zero), this returns NULL (same as miss).
 */
void* name_index_lookup(name_index_t* idx, const char* name,
                        name_index_token_t* tok_out);
/**
 * @brief Locked resolve via token gen + required name match.
 * @return value with a live ref, or NULL.
 *
 * Same hold/drop contract as @ref name_index_lookup. Also returns NULL on
 * gen mismatch / empty row / name mismatch.
 */
void* name_index_resolve(name_index_t* idx, const name_index_token_t* tok,
                         const char* name);

#endif
