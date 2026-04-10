#include <rendezvos/registry/name_index.h>
#include <common/limits.h>
#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>

#define NAME_INDEX_HT_EMPTY ((i64) - 1)
#define NAME_INDEX_HT_TOMB  ((i64) - 2)

#ifndef NAME_INDEX_ROWS_INITIAL_CAP
#define NAME_INDEX_ROWS_INITIAL_CAP (32ULL)
#endif
#ifndef NAME_INDEX_HT_INITIAL_CAP
#define NAME_INDEX_HT_INITIAL_CAP (64ULL)
#endif

#define NAME_INDEX_HT_MAX_LOAD_PERCENT 70u
#define NAME_INDEX_ROW_FREE            (0ULL)
#define NAME_INDEX_ROW_USED            (1ULL)

DEFINE_PER_CPU(struct spin_lock_t, name_index_spin_lock);

static struct allocator* name_index_allocator(name_index_t* idx)
{
        if (idx && idx->alloc)
                return idx->alloc;
        return percpu(kallocator);
}

static u32 name_index_hash_name(name_index_t* idx, const char* name)
{
        u32 hash = 2166136261u;
        if (!idx || !name || idx->name_len_max == 0)
                return 0;
        for (u32 i = 0; i < idx->name_len_max && name[i] != '\0'; i++) {
                hash ^= (u8)name[i];
                hash *= 16777619u;
        }
        return hash;
}

static void name_index_free_arrays(name_index_t* idx)
{
        if (!idx || !idx->alloc)
                return;
        if (idx->rows) {
                idx->alloc->m_free(idx->alloc, idx->rows);
                idx->rows = NULL;
        }
        if (idx->ht) {
                idx->alloc->m_free(idx->alloc, idx->ht);
                idx->ht = NULL;
        }
        idx->row_cap = 0;
        idx->ht_cap = 0;
        idx->ht_mask = 0;
        idx->ht_tombs = 0;
        idx->free_head = NAME_INDEX_FREE_HEAD_INVALID;
        idx->live = 0;
}

static error_t name_index_ht_rehash(name_index_t* idx, u64 new_ht_cap)
{
        struct allocator* alloc = name_index_allocator(idx);
        if (new_ht_cap > (u64)(SIZE_MAX / sizeof(i64)))
                return -E_RENDEZVOS;
        i64* new_ht =
                (i64*)alloc->m_alloc(alloc, (size_t)new_ht_cap * sizeof(i64));
        if (!new_ht)
                return -E_RENDEZVOS;
        for (u64 i = 0; i < new_ht_cap; i++)
                new_ht[i] = NAME_INDEX_HT_EMPTY;

        const u64 new_mask = new_ht_cap - 1;
        for (u64 row_idx = 0; row_idx < idx->row_cap; row_idx++) {
                if (idx->rows[row_idx].used != NAME_INDEX_ROW_USED
                    || !idx->rows[row_idx].storage.value)
                        continue;
                const char* n =
                        idx->get_name ?
                                idx->get_name(
                                        idx->rows[row_idx].storage.value) :
                                NULL;
                u32 h = name_index_hash_name(idx, n);
                bool placed = false;
                for (u64 probe = 0; probe < new_ht_cap; probe++) {
                        u64 bucket = ((u64)h + probe) & new_mask;
                        if (new_ht[bucket] == NAME_INDEX_HT_EMPTY
                            || new_ht[bucket] == NAME_INDEX_HT_TOMB) {
                                new_ht[bucket] = (i64)row_idx;
                                placed = true;
                                break;
                        }
                }
                if (!placed) {
                        pr_error("[name_index] ht rehash failed\n");
                        alloc->m_free(alloc, new_ht);
                        return -E_RENDEZVOS;
                }
        }

        i64* old_ht = idx->ht;
        idx->ht = new_ht;
        idx->ht_cap = new_ht_cap;
        idx->ht_mask = new_mask;
        idx->ht_tombs = 0;
        if (old_ht)
                alloc->m_free(alloc, old_ht);
        return REND_SUCCESS;
}

static error_t name_index_grow_row_array(name_index_t* idx)
{
        struct allocator* alloc = name_index_allocator(idx);
        u64 old_cap = idx->row_cap;
        u64 new_cap = old_cap ? old_cap * 2ULL : NAME_INDEX_ROWS_INITIAL_CAP;
        if (old_cap && new_cap < old_cap)
                return -E_RENDEZVOS;
        if (new_cap > (u64)(SIZE_MAX / sizeof(struct name_index_row)))
                return -E_RENDEZVOS;

        struct name_index_row* new_rows =
                (struct name_index_row*)alloc->m_alloc(
                        alloc, (size_t)new_cap * sizeof(struct name_index_row));
        if (!new_rows)
                return -E_RENDEZVOS;

        if (old_cap) {
                memcpy(new_rows,
                       idx->rows,
                       (size_t)old_cap * sizeof(struct name_index_row));
                alloc->m_free(alloc, idx->rows);
        } else {
                memset(new_rows,
                       0,
                       (size_t)new_cap * sizeof(struct name_index_row));
        }

        idx->rows = new_rows;
        idx->row_cap = new_cap;

        u64 tail = idx->free_head;
        if (old_cap == 0u) {
                for (u64 i = 0; i < new_cap; i++) {
                        idx->rows[i].used = NAME_INDEX_ROW_FREE;
                        idx->rows[i].gen = 0;
                        idx->rows[i].storage.next_free =
                                (i + 1ULL < new_cap) ?
                                        (i + 1ULL) :
                                        NAME_INDEX_FREE_HEAD_INVALID;
                }
                idx->free_head = 0u;
        } else {
                for (u64 i = old_cap; i < new_cap; i++) {
                        idx->rows[i].used = NAME_INDEX_ROW_FREE;
                        idx->rows[i].gen = 0;
                        idx->rows[i].storage.next_free =
                                (i + 1ULL < new_cap) ? (i + 1ULL) : tail;
                }
                idx->free_head = old_cap;
        }
        return REND_SUCCESS;
}

static error_t name_index_bootstrap(name_index_t* idx)
{
        if (idx->row_cap != 0u)
                return REND_SUCCESS;
        error_t e = name_index_grow_row_array(idx);
        if (e)
                return e;
        return name_index_ht_rehash(idx, NAME_INDEX_HT_INITIAL_CAP);
}

static error_t name_index_maybe_grow_ht(name_index_t* idx)
{
        if (idx->ht_cap == 0u)
                return name_index_ht_rehash(idx, NAME_INDEX_HT_INITIAL_CAP);
        if (idx->live == 0u)
                return REND_SUCCESS;
        if (idx->live * 100ULL
            <= idx->ht_cap * (u64)NAME_INDEX_HT_MAX_LOAD_PERCENT)
                return REND_SUCCESS;
        u64 nc = idx->ht_cap * 2ULL;
        if (nc < idx->ht_cap)
                return -E_RENDEZVOS;
        u64 old_cap = idx->ht_cap;
        error_t e = name_index_ht_rehash(idx, nc);
        if (e == REND_SUCCESS) {
                pr_info("[name_index] ht_cap grow %lu -> %lu (live=%lu)\n",
                        (u64)old_cap,
                        (u64)nc,
                        (u64)idx->live);
        }
        return e;
}

#define NAME_INDEX_HT_SHRINK_LOAD_PERCENT 25u
#define NAME_INDEX_HT_SHRINK_TOMB_PERCENT 10u
static void name_index_maybe_shrink_ht(name_index_t* idx)
{
        if (!idx)
                return;
        if (idx->ht_cap <= NAME_INDEX_HT_INITIAL_CAP)
                return;
        if (idx->ht_tombs == 0)
                return;

        u64 target_cap = idx->ht_cap;
        if (idx->live == 0u) {
                target_cap = NAME_INDEX_HT_INITIAL_CAP;
        } else {
                if (idx->live * 100ULL
                    > idx->ht_cap * (u64)NAME_INDEX_HT_SHRINK_LOAD_PERCENT)
                        return;
                target_cap = idx->ht_cap / 2ULL;
                if (target_cap < NAME_INDEX_HT_INITIAL_CAP)
                        target_cap = NAME_INDEX_HT_INITIAL_CAP;
        }

        if (idx->ht_tombs * 100ULL
            < idx->ht_cap * (u64)NAME_INDEX_HT_SHRINK_TOMB_PERCENT)
                return;
        if (target_cap == idx->ht_cap)
                return;

        u64 old_cap = idx->ht_cap;
        error_t e = name_index_ht_rehash(idx, target_cap);
        if (e == REND_SUCCESS) {
                pr_info("[name_index] ht_cap shrink %lu -> %lu (live=%lu tombs=%lu)\n",
                        (u64)old_cap,
                        (u64)target_cap,
                        (u64)idx->live,
                        (u64)idx->ht_tombs);
        }
}

static error_t name_index_ht_insert(name_index_t* idx, u64 row_idx,
                                    const char* name)
{
        error_t e = name_index_maybe_grow_ht(idx);
        if (e)
                return e;
        u32 h = name_index_hash_name(idx, name);
        for (u64 probe = 0; probe < idx->ht_cap; probe++) {
                u64 bucket = ((u64)h + probe) & idx->ht_mask;
                i64 cur = idx->ht[bucket];
                if (cur == NAME_INDEX_HT_EMPTY || cur == NAME_INDEX_HT_TOMB) {
                        if (cur == NAME_INDEX_HT_TOMB && idx->ht_tombs > 0)
                                idx->ht_tombs--;
                        idx->ht[bucket] = (i64)row_idx;
                        return REND_SUCCESS;
                }
        }
        e = name_index_ht_rehash(idx, idx->ht_cap * 2ULL);
        if (e)
                return e;
        return name_index_ht_insert(idx, row_idx, name);
}

static void name_index_ht_remove(name_index_t* idx, u64 row_idx,
                                 const char* name)
{
        u32 h = name_index_hash_name(idx, name);
        for (u64 probe = 0; probe < idx->ht_cap; probe++) {
                u64 bucket = ((u64)h + probe) & idx->ht_mask;
                i64 cur = idx->ht[bucket];
                if (cur == NAME_INDEX_HT_EMPTY)
                        return;
                if (cur == NAME_INDEX_HT_TOMB)
                        continue;
                if (cur != (i64)row_idx)
                        continue;
                idx->ht[bucket] = NAME_INDEX_HT_TOMB;
                idx->ht_tombs++;
                return;
        }
}

static i64 name_index_ht_find_bucket(name_index_t* idx, const char* name,
                                     u64* out_row_idx)
{
        if (!idx || !idx->ht || idx->ht_cap == 0u || !name)
                return -1;
        u32 h = name_index_hash_name(idx, name);
        for (u64 probe = 0; probe < idx->ht_cap; probe++) {
                u64 bucket = ((u64)h + probe) & idx->ht_mask;
                i64 cur = idx->ht[bucket];
                if (cur == NAME_INDEX_HT_EMPTY)
                        return -1;
                if (cur == NAME_INDEX_HT_TOMB)
                        continue;
                u64 row_idx = (u64)cur;
                if (row_idx >= idx->row_cap)
                        continue;
                if (idx->rows[row_idx].used != NAME_INDEX_ROW_USED
                    || !idx->rows[row_idx].storage.value)
                        continue;
                const char* row_name =
                        idx->get_name ?
                                idx->get_name(
                                        idx->rows[row_idx].storage.value) :
                                NULL;
                if (!row_name)
                        continue;
                if (strcmp_s(row_name, name, (size_t)idx->name_len_max) == 0) {
                        if (out_row_idx)
                                *out_row_idx = row_idx;
                        return (i64)bucket;
                }
        }
        return -1;
}

void name_index_init(name_index_t* idx, struct allocator* alloc,
                     u32 name_len_max, void* owner_context,
                     name_index_get_name_fn get_name, name_index_hold_fn hold,
                     name_index_drop_fn drop,
                     name_index_on_register_fn on_register,
                     name_index_on_unregister_fn on_unregister)
{
        if (!idx)
                return;
        memset(idx, 0, sizeof(*idx));
        idx->lock = NULL;
        idx->alloc = alloc ? alloc : percpu(kallocator);
        idx->owner_context = owner_context;
        idx->get_name = get_name;
        idx->hold = hold;
        idx->drop = drop;
        idx->on_register = on_register;
        idx->on_unregister = on_unregister;
        idx->free_head = NAME_INDEX_FREE_HEAD_INVALID;
        idx->name_len_max = name_len_max;
        (void)name_index_bootstrap(idx);
}

void name_index_fini(name_index_t* idx)
{
        if (!idx)
                return;
        struct spin_lock_t* my_lock = &percpu(name_index_spin_lock);
        lock_mcs(&idx->lock, my_lock);

        if (idx->drop) {
                for (u64 i = 0; i < idx->row_cap; i++) {
                        if (idx->rows[i].used != NAME_INDEX_ROW_USED
                            || !idx->rows[i].storage.value)
                                continue;
                        void* v = idx->rows[i].storage.value;
                        if (idx->on_unregister)
                                idx->on_unregister(v, idx->owner_context);
                        idx->rows[i].storage.value = NULL;
                        idx->rows[i].used = NAME_INDEX_ROW_FREE;
                        idx->rows[i].gen++;
                        idx->rows[i].storage.next_free = idx->free_head;
                        idx->free_head = i;
                        if (idx->live)
                                idx->live--;
                        idx->drop(v);
                }
        }

        name_index_free_arrays(idx);
        unlock_mcs(&idx->lock, my_lock);
}

void* name_index_search(name_index_t* idx, const char* name, u64* out_row_idx)
{
        if (!idx || !name)
                return NULL;
        u64 row_idx = 0;
        i64 bucket = name_index_ht_find_bucket(idx, name, &row_idx);
        if (bucket < 0)
                return NULL;
        if (out_row_idx)
                *out_row_idx = row_idx;
        return idx->rows[row_idx].storage.value;
}

error_t name_index_register(name_index_t* idx, void* value,
                            u64* out_reg_row_idx)
{
        if (!idx || !value || !idx->get_name)
                return -E_IN_PARAM;
        const char* name = idx->get_name(value);
        if (!name || !name[0])
                return -E_IN_PARAM;

        error_t e = name_index_bootstrap(idx);
        if (e)
                return e;

        if (idx->free_head == NAME_INDEX_FREE_HEAD_INVALID) {
                e = name_index_grow_row_array(idx);
                if (e)
                        return e;
        }
        u64 row_idx = idx->free_head;
        if (row_idx >= idx->row_cap)
                return -E_RENDEZVOS;
        if (idx->rows[row_idx].used != NAME_INDEX_ROW_FREE)
                return -E_RENDEZVOS;

        idx->free_head = idx->rows[row_idx].storage.next_free;
        idx->rows[row_idx].storage.value = value;
        idx->rows[row_idx].used = NAME_INDEX_ROW_USED;
        idx->live++;

        e = name_index_ht_insert(idx, row_idx, name);
        if (e) {
                idx->rows[row_idx].storage.value = NULL;
                idx->rows[row_idx].used = NAME_INDEX_ROW_FREE;
                idx->rows[row_idx].storage.next_free = idx->free_head;
                idx->free_head = row_idx;
                if (idx->live)
                        idx->live--;
                return e;
        }

        if (idx->on_register)
                idx->on_register(value, idx->owner_context);
        if (out_reg_row_idx)
                *out_reg_row_idx = row_idx;
        return REND_SUCCESS;
}

void name_index_register_abort(name_index_t* idx, u64 row_idx, void* value)
{
        if (!idx)
                return;
        if (row_idx >= idx->row_cap)
                return;
        if (idx->rows[row_idx].used != NAME_INDEX_ROW_USED
            || idx->rows[row_idx].storage.value != value)
                return;
        const char* name = idx->get_name ? idx->get_name(value) : NULL;
        if (name)
                name_index_ht_remove(idx, row_idx, name);
        if (idx->on_unregister)
                idx->on_unregister(value, idx->owner_context);
        idx->rows[row_idx].storage.value = NULL;
        idx->rows[row_idx].used = NAME_INDEX_ROW_FREE;
        idx->rows[row_idx].gen++;
        idx->rows[row_idx].storage.next_free = idx->free_head;
        idx->free_head = row_idx;
        if (idx->live)
                idx->live--;
        name_index_maybe_shrink_ht(idx);
}

void name_index_unregister(name_index_t* idx, void* value, u64 row_idx,
                           const char* name)
{
        if (!idx || row_idx >= idx->row_cap)
                return;
        if (idx->rows[row_idx].used != NAME_INDEX_ROW_USED
            || idx->rows[row_idx].storage.value != value)
                return;
        if (name)
                name_index_ht_remove(idx, row_idx, name);
        if (idx->on_unregister)
                idx->on_unregister(value, idx->owner_context);
        idx->rows[row_idx].storage.value = NULL;
        idx->rows[row_idx].used = NAME_INDEX_ROW_FREE;
        idx->rows[row_idx].gen++;
        idx->rows[row_idx].storage.next_free = idx->free_head;
        idx->free_head = row_idx;
        if (idx->live)
                idx->live--;
        name_index_maybe_shrink_ht(idx);
}

void* name_index_lookup(name_index_t* idx, const char* name,
                        name_index_token_t* tok_out)
{
        if (!idx || !name)
                return NULL;
        struct spin_lock_t* my_lock = &percpu(name_index_spin_lock);
        lock_mcs(&idx->lock, my_lock);

        u64 row_idx = 0;
        void* v = name_index_search(idx, name, &row_idx);
        if (!v || idx->rows[row_idx].used != NAME_INDEX_ROW_USED) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }

        if (tok_out) {
                tok_out->row_index = (u32)row_idx;
                tok_out->row_gen = (u16)idx->rows[row_idx].gen;
        }

        if (idx->hold && !idx->hold(v)) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }
        unlock_mcs(&idx->lock, my_lock);
        return v;
}

void* name_index_resolve(name_index_t* idx, const name_index_token_t* tok,
                         const char* name)
{
        if (!idx || !tok || !name)
                return NULL;
        if (tok->row_index == NAME_INDEX_ROW_INDEX_INVALID)
                return NULL;

        struct spin_lock_t* my_lock = &percpu(name_index_spin_lock);
        lock_mcs(&idx->lock, my_lock);

        u64 row_idx = (u64)tok->row_index;
        if (row_idx >= idx->row_cap) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }
        if (idx->rows[row_idx].used != NAME_INDEX_ROW_USED
            || !idx->rows[row_idx].storage.value) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }
        if ((u16)idx->rows[row_idx].gen != tok->row_gen) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }
        const char* row_name =
                idx->get_name ?
                        idx->get_name(idx->rows[row_idx].storage.value) :
                        NULL;
        if (!row_name
            || strcmp_s(row_name, name, (size_t)idx->name_len_max) != 0) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }

        void* v = idx->rows[row_idx].storage.value;
        if (idx->hold && !idx->hold(v)) {
                unlock_mcs(&idx->lock, my_lock);
                return NULL;
        }

        unlock_mcs(&idx->lock, my_lock);
        return v;
}
