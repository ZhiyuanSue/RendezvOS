#include "port_table_slots.h"
#include <common/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>
#include <common/refcount.h>
#include <common/string.h>
#include <modules/log/log.h>

static struct allocator* port_slots_allocator(struct Port_Table* t)
{
        if (t && t->alloc)
                return t->alloc;
        return percpu(kallocator);
}

/* Name hash used by ht (slot array is still the source of truth). */
static u32 port_hash_name(const char* name)
{
        u32 hash = 2166136261u;
        if (!name)
                return 0;
        for (u32 i = 0; i < PORT_NAME_LEN_MAX && name[i] != '\0'; i++) {
                hash ^= (u8)name[i];
                hash *= 16777619u;
        }
        return hash;
}

static void port_slots_free_arrays(struct Port_Table* t)
{
        if (!t->alloc)
                return;
        if (t->slots) {
                t->alloc->m_free(t->alloc, t->slots);
                t->slots = NULL;
        }
        if (t->ht) {
                t->alloc->m_free(t->alloc, t->ht);
                t->ht = NULL;
        }
        t->slot_cap = 0;
        t->ht_cap = 0;
        t->ht_mask = 0;
        t->ht_tombs = 0;
        t->free_head = PORT_TABLE_SLOT_FREE_INVALID;
        t->live_ports = 0;
}

/*
 * Rebuild ht from current live slots (two-phase):
 * 1) build temporary new_ht completely
 * 2) swap table pointers only after full success
 */
static error_t port_slots_ht_rehash(struct Port_Table* t, u64 new_ht_cap)
{
        struct allocator* a = port_slots_allocator(t);
        if (new_ht_cap > (u64)(SIZE_MAX / sizeof(i64)))
                return -E_RENDEZVOS;
        i64* new_ht = (i64*)a->m_alloc(a, (size_t)new_ht_cap * sizeof(i64));
        if (!new_ht)
                return -E_RENDEZVOS;
        for (u64 i = 0; i < new_ht_cap; i++)
                new_ht[i] = PORT_HT_EMPTY;

        const u64 new_mask = new_ht_cap - 1;
        for (u64 slot_idx = 0; slot_idx < t->slot_cap; slot_idx++) {
                if (t->slots[slot_idx].used != PORT_SLOT_USED
                    || !t->slots[slot_idx].storage.port)
                        continue;
                u32 h = port_hash_name(t->slots[slot_idx].storage.port->name);
                bool placed = false;
                for (u64 probe = 0; probe < new_ht_cap; probe++) {
                        u64 idx = ((u64)h + probe) & new_mask;
                        if (new_ht[idx] == PORT_HT_EMPTY
                            || new_ht[idx] == PORT_HT_TOMB) {
                                new_ht[idx] = (i64)slot_idx;
                                placed = true;
                                break;
                        }
                }
                if (!placed) {
                        pr_error("[port_slots] ht rehash failed\n");
                        a->m_free(a, new_ht);
                        return -E_RENDEZVOS;
                }
        }

        i64* old_ht = t->ht;
        t->ht = new_ht;
        t->ht_cap = new_ht_cap;
        t->ht_mask = new_mask;
        t->ht_tombs = 0;
        if (old_ht)
                a->m_free(a, old_ht);
        return REND_SUCCESS;
}

static error_t port_slots_grow_slot_array(struct Port_Table* t)
{
        struct allocator* a = port_slots_allocator(t);
        u64 old_cap = t->slot_cap;
        u64 new_cap = old_cap ? old_cap * 2ULL : PORT_SLOTS_INITIAL_CAP;
        if (old_cap && new_cap < old_cap)
                return -E_RENDEZVOS;
        if (new_cap > (u64)(SIZE_MAX / sizeof(struct port_slot)))
                return -E_RENDEZVOS;
        struct port_slot* new_slots = (struct port_slot*)a->m_alloc(
                a, (size_t)new_cap * sizeof(struct port_slot));
        if (!new_slots)
                return -E_RENDEZVOS;

        if (old_cap) {
                memcpy(new_slots,
                       t->slots,
                       (size_t)old_cap * sizeof(struct port_slot));
                a->m_free(a, t->slots);
        } else {
                memset(new_slots,
                       0,
                       (size_t)new_cap * sizeof(struct port_slot));
        }

        t->slots = new_slots;
        t->slot_cap = new_cap;

        u64 tail = t->free_head;
        if (old_cap == 0u) {
                for (u64 i = 0; i < new_cap; i++) {
                        t->slots[i].used = PORT_SLOT_FREE;
                        t->slots[i].gen = 0;
                        t->slots[i].storage.next_free =
                                (i + 1ULL < new_cap) ?
                                        (i + 1ULL) :
                                        PORT_TABLE_SLOT_FREE_INVALID;
                }
                t->free_head = 0u;
        } else {
                for (u64 i = old_cap; i < new_cap; i++) {
                        t->slots[i].used = PORT_SLOT_FREE;
                        t->slots[i].gen = 0;
                        t->slots[i].storage.next_free =
                                (i + 1ULL < new_cap) ? (i + 1ULL) : tail;
                }
                t->free_head = old_cap;
        }
        return REND_SUCCESS;
}

static error_t port_slots_bootstrap(struct Port_Table* t)
{
        if (t->slot_cap != 0u)
                return REND_SUCCESS;
        error_t e = port_slots_grow_slot_array(t);
        if (e)
                return e;
        return port_slots_ht_rehash(t, PORT_HT_INITIAL_CAP);
}

static error_t port_slots_maybe_grow_ht(struct Port_Table* t)
{
        if (t->ht_cap == 0u)
                return port_slots_ht_rehash(t, PORT_HT_INITIAL_CAP);
        if (t->live_ports == 0u)
                return REND_SUCCESS;
        if (t->live_ports * 100ULL <= t->ht_cap * (u64)PORT_HT_MAX_LOAD_PERCENT)
                return REND_SUCCESS;
        u64 nc = t->ht_cap * 2ULL;
        if (nc < t->ht_cap)
                return -E_RENDEZVOS;
        u64 old_cap = t->ht_cap;
        error_t e = port_slots_ht_rehash(t, nc);
        if (e == REND_SUCCESS) {
                pr_info("[port_slots] ht_cap grow %u -> %u (live=%u)\n",
                        (u64)old_cap,
                        (u64)nc,
                        (u64)t->live_ports);
        }
        return e;
}

/* Tomb-aware shrink heuristic: rebuild ht with smaller capacity to drop tombs. */
#define PORT_HT_SHRINK_LOAD_PERCENT 25u
#define PORT_HT_SHRINK_TOMB_PERCENT 10u
static void port_slots_maybe_shrink_ht(struct Port_Table* t)
{
        if (!t)
                return;
        if (t->ht_cap <= PORT_HT_INITIAL_CAP)
                return;
        if (t->ht_tombs == 0)
                return;

        u64 target_cap = t->ht_cap;
        if (t->live_ports == 0u) {
                target_cap = PORT_HT_INITIAL_CAP;
        } else {
                /* Only shrink when live load is low enough (hysteresis). */
                if (t->live_ports * 100ULL
                    > t->ht_cap * (u64)PORT_HT_SHRINK_LOAD_PERCENT)
                        return;
                target_cap = t->ht_cap / 2ULL;
                if (target_cap < PORT_HT_INITIAL_CAP)
                        target_cap = PORT_HT_INITIAL_CAP;
        }

        /* Require tomb density to be meaningful to avoid shrink churn. */
        if (t->ht_tombs * 100ULL < t->ht_cap * (u64)PORT_HT_SHRINK_TOMB_PERCENT)
                return;
        if (target_cap == t->ht_cap)
                return;

        u64 old_cap = t->ht_cap;
        error_t e = port_slots_ht_rehash(t, target_cap);
        if (e == REND_SUCCESS) {
                pr_info("[port_slots] ht_cap shrink %u -> %u (live=%u tombs=%u)\n",
                        (u64)old_cap,
                        (u64)target_cap,
                        (u64)t->live_ports,
                        (u64)t->ht_tombs);
        }
}

/* Insert mapping: name -> slot_idx into open-addressing ht. */
static error_t port_slots_ht_insert(struct Port_Table* t, u64 slot_idx,
                                    const char* name)
{
        error_t e = port_slots_maybe_grow_ht(t);
        if (e)
                return e;
        u32 h = port_hash_name(name);
        for (u64 probe = 0; probe < t->ht_cap; probe++) {
                u64 idx = ((u64)h + probe) & t->ht_mask;
                i64 cur = t->ht[idx];
                if (cur == PORT_HT_EMPTY || cur == PORT_HT_TOMB) {
                        if (cur == PORT_HT_TOMB && t->ht_tombs > 0)
                                t->ht_tombs--;
                        t->ht[idx] = (i64)slot_idx;
                        return REND_SUCCESS;
                }
        }
        e = port_slots_ht_rehash(t, t->ht_cap * 2ULL);
        if (e)
                return e;
        return port_slots_ht_insert(t, slot_idx, name);
}

/* Remove mapping by marking matching bucket as tomb (preserve probe chain). */
static void port_slots_ht_remove(struct Port_Table* t, u64 slot_idx,
                                 const char* name)
{
        u32 h = port_hash_name(name);
        for (u64 probe = 0; probe < t->ht_cap; probe++) {
                u64 idx = ((u64)h + probe) & t->ht_mask;
                i64 cur = t->ht[idx];
                if (cur == PORT_HT_EMPTY)
                        return;
                if (cur == PORT_HT_TOMB)
                        continue;
                if (cur != (i64)slot_idx)
                        continue;
                Message_Port_t* registered_port =
                        t->slots[slot_idx].storage.port;
                if (registered_port
                    && strcmp(registered_port->name, name) == 0) {
                        t->ht[idx] = PORT_HT_TOMB;
                        t->ht_tombs++;
                        return;
                }
        }
}

static void port_table_link_port(Message_Port_t* port, struct Port_Table* table)
{
        port->table = table;
        port->registered = true;
}

static void port_table_unlink_port(Message_Port_t* port)
{
        port->table = NULL;
        port->registered = false;
}

static error_t port_slots_alloc_slot(struct Port_Table* t, u64* out_slot_idx)
{
        if (t->free_head == PORT_TABLE_SLOT_FREE_INVALID) {
                if (port_slots_grow_slot_array(t))
                        return -E_RENDEZVOS;
        }
        if (t->free_head == PORT_TABLE_SLOT_FREE_INVALID)
                return -E_RENDEZVOS;
        u64 slot_idx = t->free_head;
        t->free_head = t->slots[slot_idx].storage.next_free;
        t->slots[slot_idx].used = PORT_SLOT_USED;
        *out_slot_idx = slot_idx;
        return REND_SUCCESS;
}

/* Free one slot back to freelist and bump generation to invalidate old tokens. */
static void port_slots_free_slot(struct Port_Table* t, u64 slot_idx)
{
        if (slot_idx >= t->slot_cap)
                return;
        t->slots[slot_idx].gen++;
        t->slots[slot_idx].used = PORT_SLOT_FREE;
        t->slots[slot_idx].storage.next_free = t->free_head;
        t->free_head = slot_idx;
}

void port_slots_table_init(struct Port_Table* t)
{
        t->slots = NULL;
        t->slot_cap = 0;
        t->free_head = PORT_TABLE_SLOT_FREE_INVALID;
        t->live_ports = 0;
        t->ht = NULL;
        t->ht_cap = 0;
        t->ht_mask = 0;
        t->ht_tombs = 0;
        if (port_slots_bootstrap(t))
                pr_error("[port_slots] bootstrap failed\n");
}

void port_slots_table_fini(struct Port_Table* t)
{
        if (!t)
                return;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);

        for (;;) {
                lock_mcs(&t->lock, my_lock);
                if (t->live_ports == 0u || !t->slots) {
                        unlock_mcs(&t->lock, my_lock);
                        break;
                }
                Message_Port_t* port_to_drop = NULL;
                u64 slot_idx = 0;
                const char* name = NULL;
                for (u64 i = 0; i < t->slot_cap; i++) {
                        struct port_slot* slot = &t->slots[i];
                        if (slot->used != PORT_SLOT_USED
                            || !slot->storage.port)
                                continue;
                        port_to_drop = slot->storage.port;
                        slot_idx = i;
                        name = port_to_drop->name;
                        break;
                }
                if (!port_to_drop || !name) {
                        pr_error(
                                "[port_slots] fini: live_ports=%u but no slot (corrupt?)\n",
                                (u64)t->live_ports);
                        unlock_mcs(&t->lock, my_lock);
                        break;
                }
                port_slots_unregister(t, port_to_drop, slot_idx, name);
                unlock_mcs(&t->lock, my_lock);
                ref_put(&port_to_drop->refcount, free_message_port_ref);
        }

        port_slots_free_arrays(t);
}

Message_Port_t* port_slots_search(struct Port_Table* t, const char* name,
                                  u64* out_slot_idx)
{
        if (!t->ht || t->ht_cap == 0u)
                return NULL;
        u32 h = port_hash_name(name);
        for (u64 probe = 0; probe < t->ht_cap; probe++) {
                u64 idx = ((u64)h + probe) & t->ht_mask;
                i64 ht_slot = t->ht[idx];
                if (ht_slot == PORT_HT_EMPTY)
                        return NULL;
                if (ht_slot == PORT_HT_TOMB)
                        continue;
                if (ht_slot < 0 || (u64)ht_slot >= t->slot_cap)
                        continue;
                struct port_slot* slot = &t->slots[(u64)ht_slot];
                if (slot->used != PORT_SLOT_USED || !slot->storage.port)
                        continue;
                if (strcmp(slot->storage.port->name, name) == 0) {
                        if (out_slot_idx)
                                *out_slot_idx = (u64)ht_slot;
                        return slot->storage.port;
                }
        }
        return NULL;
}

/* Register path: alloc slot -> write slot -> insert ht mapping -> link port. */
error_t port_slots_register(struct Port_Table* table, Message_Port_t* port,
                            u64* out_reg_slot_idx)
{
        u64 slot_idx;
        error_t err = port_slots_alloc_slot(table, &slot_idx);
        if (err)
                return err;
        table->slots[slot_idx].storage.port = port;
        err = port_slots_ht_insert(table, slot_idx, port->name);
        if (err) {
                table->slots[slot_idx].storage.port = NULL;
                table->slots[slot_idx].used = PORT_SLOT_FREE;
                table->slots[slot_idx].storage.next_free = table->free_head;
                table->free_head = slot_idx;
                return err;
        }
        port_table_link_port(port, table);
        table->live_ports++;
        *out_reg_slot_idx = slot_idx;
        return REND_SUCCESS;
}

void port_slots_register_abort(struct Port_Table* table, u64 slot_idx,
                               Message_Port_t* port)
{
        port_slots_ht_remove(table, slot_idx, port->name);
        port_table_unlink_port(port);
        table->slots[slot_idx].storage.port = NULL;
        if (table->live_ports > 0u)
                table->live_ports--;
        port_slots_free_slot(table, slot_idx);
        port_slots_maybe_shrink_ht(table);
}

/* Unregister path: remove ht mapping -> unlink/clear slot -> freelist + gen++. */
void port_slots_unregister(struct Port_Table* table, Message_Port_t* port,
                           u64 slot_idx, const char* name)
{
        port_slots_ht_remove(table, slot_idx, name);
        port_table_unlink_port(port);
        table->slots[slot_idx].storage.port = NULL;
        if (table->live_ports > 0u)
                table->live_ports--;
        port_slots_free_slot(table, slot_idx);
        port_slots_maybe_shrink_ht(table);
}

static Message_Port_t*
port_slots_lookup_locked(struct Port_Table* table, const char* name,
                         port_table_slot_token_t* tok_out)
{
        u64 slot_idx = 0;
        Message_Port_t* port = port_slots_search(table, name, &slot_idx);
        if (!port || !port->registered) {
                if (tok_out)
                        port_table_slot_token_invalidate(tok_out);
                return NULL;
        }
        if (!ref_get_not_zero(&port->refcount)) {
                if (tok_out)
                        port_table_slot_token_invalidate(tok_out);
                return NULL;
        }
        if (tok_out) {
                tok_out->slot_index = (u32)slot_idx;
                tok_out->slot_gen = (u16)table->slots[slot_idx].gen;
        }
        return port;
}

Message_Port_t* port_slots_lookup(struct Port_Table* table, const char* name,
                                  port_table_slot_token_t* tok_out)
{
        if (!table || !name)
                return NULL;
        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* found = port_slots_lookup_locked(table, name, tok_out);
        unlock_mcs(&table->lock, my_lock);
        return found;
}

Message_Port_t* port_slots_resolve(struct Port_Table* table,
                                   const port_table_slot_token_t* tok,
                                   const char* name)
{
        if (!table || !tok || !name)
                return NULL;
        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* port = NULL;
        if (tok->slot_index == PORT_TABLE_SLOT_INDEX_INVALID) {
                port = port_slots_lookup_locked(table, name, NULL);
                unlock_mcs(&table->lock, my_lock);
                return port;
        }
        if ((u64)tok->slot_index >= table->slot_cap)
                goto out_null;
        struct port_slot* slot = &table->slots[(u64)tok->slot_index];
        if (slot->used != PORT_SLOT_USED || (u16)slot->gen != tok->slot_gen)
                goto out_null;
        port = slot->storage.port;
        if (!port || strcmp(port->name, name) != 0 || !port->registered)
                goto out_null;
        if (!ref_get_not_zero(&port->refcount))
                goto out_null;
        unlock_mcs(&table->lock, my_lock);
        return port;
out_null:
        unlock_mcs(&table->lock, my_lock);
        return NULL;
}
