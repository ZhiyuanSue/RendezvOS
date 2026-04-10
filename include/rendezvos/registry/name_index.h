#ifndef _RENDEZVOS_NAME_INDEX_H_
#define _RENDEZVOS_NAME_INDEX_H_

#include <common/types.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/spin_lock.h>

/*
 * name_index: global string-keyed index (implementation: slot array + OA hash +
 * per-row generation for cache tokens).
 *
 * - Register/lookup by NUL-terminated name (length bounded by name_len_max).
 * - Optional refcount hooks (hold/drop) for lookup paths that return a live
 * ref.
 * - Optional callbacks when a value is linked/unlinked from the index
 *   (e.g. port->registered / port->table).
 *
 * Not a per-thread cache; callers may cache (row_index, row_gen) in
 * name_index_token_t and resolve under the index lock.
 */

/** Freelist head when empty (never a valid row index). */
#define NAME_INDEX_FREE_HEAD_INVALID U64_MAX
/** Invalid row index in a cache token (never a valid row index). */
#define NAME_INDEX_ROW_INDEX_INVALID ((u32) - 1)

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

void name_index_init(name_index_t* idx, struct allocator* alloc,
                     u32 name_len_max, void* owner_context,
                     name_index_get_name_fn get_name, name_index_hold_fn hold,
                     name_index_drop_fn drop,
                     name_index_on_register_fn on_register,
                     name_index_on_unregister_fn on_unregister);

void name_index_fini(name_index_t* idx);

void* name_index_search(name_index_t* idx, const char* name, u64* out_row_idx);

error_t name_index_register(name_index_t* idx, void* value,
                            u64* out_reg_row_idx);
void name_index_register_abort(name_index_t* idx, u64 row_idx, void* value);
void name_index_unregister(name_index_t* idx, void* value, u64 row_idx,
                           const char* name);

void* name_index_lookup(name_index_t* idx, const char* name,
                        name_index_token_t* tok_out);
void* name_index_resolve(name_index_t* idx, const name_index_token_t* tok,
                         const char* name);

#endif
