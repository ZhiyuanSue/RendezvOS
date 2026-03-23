#ifndef _PORT_TABLE_SLOTS_H_
#define _PORT_TABLE_SLOTS_H_

#include <rendezvos/task/port.h>
#include <rendezvos/mm/allocator.h>

#define PORT_HT_EMPTY            ((i64) - 1)
#define PORT_HT_TOMB             ((i64) - 2)
#define PORT_HT_MAX_LOAD_PERCENT 70u

/*
 * Mapping model (source of truth + derived index):
 *
 *   slots[slot_idx]        : source of truth for port object/lifetime.
 *   ht[bucket] -> slot_idx : derived name index for fast lookup.
 *
 * Hash table stores slot indices (not pointers):
 *   PORT_HT_EMPTY : end of probe chain
 *   PORT_HT_TOMB  : deleted bucket, keep probing
 *   >= 0          : valid slot index in slots[]
 *
 * Operation categories:
 * - Register:
 *   allocate slot from freelist -> write slots[slot_idx].port ->
 *   insert (name -> slot_idx) into ht.
 * - Unregister:
 *   remove ht mapping (mark tomb) -> clear/unlink slot -> bump gen ->
 *   push slot back to freelist.
 * - Lookup:
 *   probe ht by name -> read slot_idx -> validate slot + final strcmp(name).
 * - Rehash:
 *   rebuild ht from all live slots, then swap in one commit.
 */

/*
 * Initialize/teardown the slots backend storage in Port_Table.
 * - init: bootstrap slot array + hash table (expects t->alloc set by
 * port_table_init).
 * - fini: drops table-held refs on still-registered ports (unregister + ref_put
 * like unregister_port), then frees slot/ht arrays with t->alloc (or a
 * fallback). Caller still frees the Port_Table struct with the same allocator.
 */
void port_slots_table_init(struct Port_Table* t);
void port_slots_table_fini(struct Port_Table* t);

/*
 * Name search without refcount changes.
 * - Locking: does not lock internally; caller must hold table lock when needed.
 * - out_slot_idx: optional slot index of matched entry.
 */
Message_Port_t* port_slots_search(struct Port_Table* table, const char* name,
                                  u64* out_slot_idx);

/*
 * Register a new port into slots+hash.
 * - Requires caller holds table lock.
 * - On success, links port->table/registered and returns slot index.
 */
error_t port_slots_register(struct Port_Table* table, Message_Port_t* port,
                            u64* out_reg_slot_idx);

/*
 * Remove an existing port using known slot index.
 * - Requires caller holds table lock.
 * - Unlinks table ownership, removes hash entry, bumps slot generation.
 */
void port_slots_unregister(struct Port_Table* table, Message_Port_t* port,
                           u64 slot_idx, const char* name);

/*
 * Roll back a successful register when upper-layer ref_get fails.
 * - Requires caller holds table lock.
 * - Keeps table/port state consistent with "never registered".
 */
void port_slots_register_abort(struct Port_Table* table, u64 slot_idx,
                               Message_Port_t* port);

/*
 * Lookup by name with lock + ref_get_not_zero inside.
 * - Returns one live reference on success (caller must ref_put).
 * - Optionally exports a stable slot token (index+generation) for cache.
 */
Message_Port_t* port_slots_lookup(struct Port_Table* table, const char* name,
                                  port_table_slot_token_t* tok_out);

/*
 * Resolve cached token under lock.
 * - Validates slot index/generation/name, then ref_get_not_zero.
 * - Returns one live reference on success (caller must ref_put).
 */
Message_Port_t* port_slots_resolve(struct Port_Table* table,
                                   const port_table_slot_token_t* tok,
                                   const char* name);

#endif
