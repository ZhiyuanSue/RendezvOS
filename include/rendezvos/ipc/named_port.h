#ifndef _RENDEZVOS_NAMED_PORT_H_
#define _RENDEZVOS_NAMED_PORT_H_

#include <rendezvos/ipc/port.h>
#include <rendezvos/registry/name_index.h>

/*
 * Named port = directory / discovery shell over an embedded anonymous
 * rendezvous (@c PORT_COMMON at offset 0, same prefix as @c Message_Port_t).
 *
 * Two axes (do not conflate):
 *   1) Rendezvous life — @c PORT_OPS_LIFE_* on COMMON (OPEN / CLOSING /
 * CLOSED).
 *      @c port_ops_begin / @c send_msg / @c recv_msg only care about a OPEN
 *      anonymous port. @c close_port drains waiters. Independent of whether the
 *      name is in a table.
 *   2) Directory membership — @c port->table / @c name[] /
 *      name_index. create → not published; register → visible to lookup;
 *      unregister → close embedded anon, then leave the table and drop the
 *      table pin.
 *
 * Admission (@c ops_allow) is named port-only policy, not anon life:
 *   - LOOKUP / REGISTER: port-table paths.
 *   - SEND / RECV: only via @c named_port_send_msg / @c named_port_recv_msg /
 *     @c named_port_try_* (allow, then anon ipc). Anon @c port_ops_begin should
 *     **not** call @c ops_allow.
 *   - @c lookup_name may be non-NULL only for LOOKUP; REGISTER/SEND/RECV pass
 *     NULL (use @c port->name). Deny ≠ close / unregister; already-blocked
 *     waiters are not re-checked on wake.
 *
 * Who calls what:
 *   - Named business IPC → @c named_port_send_msg / @c named_port_recv_msg /
 *     @c named_port_try_* only. Never cast to @c Message_Port_t* for send/recv
 *     (skips SEND/RECV allow).
 *   - Pure anon ports (@c create_message_port) → @c send_msg / @c recv_msg /
 *     @c close_port; holding the pointer is the allow.
 *   - Refs: @c free_named_message_port_ref only — never
 *     @c free_message_port_ref / @c delete_message_port_structure on a named
 *     object (the embedded anon is not a standalone heap @c Message_Port_t).
 */

/* Default initial slot capacity for a new port table name index. */
#ifndef PORT_SLOTS_INITIAL_CAP
#define PORT_SLOTS_INITIAL_CAP (32ULL)
#endif
/* Default initial hash-table capacity for a new port table name index. */
#ifndef PORT_HT_INITIAL_CAP
#define PORT_HT_INITIAL_CAP (64ULL)
#endif

/** Max port name length including trailing NUL. */
#define PORT_NAME_LEN_MAX 64

typedef struct Named_Msg_Port Named_Message_Port_t;

/**
 * @brief Optional hook on first named-port allocation (before
 *        @c register_named_port).
 * @param port Port to initialize; append bytes at @c port->append_info when
 *        @c append_info_len is non-zero.
 * @return @c REND_SUCCESS on success; any other @c error_t aborts create (alloc
 *         rolled back).
 */
typedef error_t (*port_append_init_t)(Named_Message_Port_t* port);

/**
 * @brief Optional hook before named-port memory is freed
 *        (@c free_named_message_port_ref, after ensure-CLOSED).
 * @param port Port being destroyed.
 */
typedef void (*port_append_fini_t)(Named_Message_Port_t* port);

/**
 * @brief Gate operation kinds for @c port_ops_allow_t.
 *
 * Discovery: LOOKUP / REGISTER (port table paths).
 * IPC: SEND / RECV via @c named_port_send_msg / @c named_port_recv_msg / @c
 * named_port_try_* only — **not** via anon @c port_ops_begin (that gates solely
 * on
 * @c PORT_OPS_LIFE_OPEN).
 * Not to be confused with @c PORT_OPS_LIFE_* (anon rendezvous lifecycle).
 */
enum port_ops_type {
        PORT_OPS_LOOKUP,
        PORT_OPS_REGISTER,
        PORT_OPS_SEND,
        PORT_OPS_RECV,
};

/**
 * @brief Optional admission gate (NULL = allow).
 *
 * Invoked on the **accessor CPU context**: policy must use
 * @c get_cpu_current_thread() (and thread append) to identify who is acting.
 * Core does not pass a separate actor and does not implement
 * namespace/capability tables; the name-index key is whatever string was
 * registered.
 *
 * @param port Target named port (looked up, registered, or IPC target).
 * @param op_type Which gate fired.
 * @param lookup_name Index key; **non-NULL only legal for**
 *        @c PORT_OPS_LOOKUP. REGISTER / SEND / RECV must pass @c NULL
 *        (policy uses @c port->name). Core rejects mismatched pairs before
 *        invoking this callback (same rule as pre-split @c port_ops_begin
 *        for SEND/RECV, which always passed @c NULL).
 * @return @c REND_SUCCESS to allow; any other @c error_t denies. Prefer
 *         @c -E_REND_ACCESS for policy reject (not @c -E_REND_PORT_CLOSED):
 *         LOOKUP → drop hold + NULL (errno discarded);
 *         REGISTER → that @c error_t from @c register_named_port;
 *         SEND/RECV → that @c error_t from @c named_port_*_msg (ipc not
 * entered).
 *
 * @note @c PORT_OPS_REGISTER runs **under the port-table MCS lock** — must not
 *       re-enter @c register_named_port / @c port_table_lookup on the same
 *       table (deadlock). Keep the callback fast; do not @c schedule.
 * @note SEND/RECV checks run outside the table lock (before anon ipc). Do not
 *       @c schedule inside the callback.
 * @note Deny ≠ unregister: port stays in the table if already published;
 *       waiters already past @c port_ops_begin are not re-checked on wake.
 */
typedef error_t (*port_ops_allow_t)(Named_Message_Port_t* port,
                                    enum port_ops_type op_type,
                                    const char* lookup_name);

/**
 * @brief Named-port append lifecycle + discovery admission gates.
 *
 * Stored as a pointer on each named port; upper layers usually pass one static
 * table. @c ops_allow and @c init/@c fini may be NULL (allow / no-op).
 * @c append_info_len bytes follow the struct as FAM (@c append_info).
 */
typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;

/**
 * @brief Named directory object with embedded anonymous rendezvous.
 *
 * Layout: @c PORT_COMMON first (same prefix as @c Message_Port_t), then
 * directory / name / hooks / FAM.
 */
struct Named_Msg_Port {
        PORT_COMMON;
        struct Port_Table* table; /* Owning table if registered; else NULL. */
        char name[PORT_NAME_LEN_MAX]; /* Port name / name_index key. */
        const struct port_append_hooks* append_hooks;
        char append_info[]; /* FAM; length @c append_hooks->append_info_len. */
};

/** True if @p port is currently linked into a @c Port_Table. */
static inline bool named_port_is_in_table(const Named_Message_Port_t* port)
{
        return port && port->table != NULL;
}

/** Append info length from hooks, or 0. */
static inline size_t
named_port_append_info_len(const Named_Message_Port_t* port)
{
        return (port && port->append_hooks) ?
                       port->append_hooks->append_info_len :
                       0;
}

/** Bytes to allocate for a named port including optional append info. */
static inline size_t
named_message_port_total_size(const port_append_hooks_t* hooks)
{
        size_t n = sizeof(Named_Message_Port_t);

        if (hooks && hooks->append_info_len)
                n += hooks->append_info_len;
        return n;
}

/* Global port table: string-keyed index over Named_Message_Port_t (see
 * name_index). */
struct Port_Table {
        name_index_t by_name;
};

extern struct spin_lock_t port_table_spin_lock;

/**
 * @brief Heap-allocate an unregistered named port.
 * @param name Port name / name_index key (non-empty, shorter than
 *        @c PORT_NAME_LEN_MAX).
 * @param hooks Optional append table; NULL for no append and all gates open.
 * @return New port with refcount 1, embedded life @c OPEN, not in any table;
 *         or NULL on invalid name, allocation failure, or @c hooks->init
 *         failure (rolls back alloc).
 * @note Sets @c service_id from the name (never 0). Does **not** register.
 */
Named_Message_Port_t*
create_named_message_port(const char* name, const port_append_hooks_t* hooks);

/**
 * @brief Refcount destructor for heap @c Named_Message_Port_t.
 *
 * Ensures the embedded rendezvous is @c CLOSED (@c close_port once if OPEN;
 * already CLOSED ok; CLOSING or close failure → error, no @c m_free).
 * Then runs append @c fini and @c m_free s the Named object.
 *
 * Prefer already-CLOSED via @c unregister_named_port.
 * Do not use @c free_message_port_ref / @c delete_message_port_structure.
 */
error_t free_named_message_port_ref(ref_count_t* ref_count_ptr);

/**
 * @brief Allocate a port table and initialize its name index.
 * @return New @c Port_Table, or NULL on allocation failure.
 */
struct Port_Table* port_table_create(void);

/**
 * @brief Initialize an existing @c Port_Table name index.
 * @param table Table to initialize; no-op if NULL.
 */
void port_table_init(struct Port_Table* table);

/**
 * @brief Tear down a port table and free its backing storage.
 * @param table Table to destroy; no-op if NULL.
 */
void delete_port_table_structure(struct Port_Table* table);

/**
 * @brief Look up a registered named port by name and hold a reference.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @return Named port with refcount incremented, or NULL if not found, invalid
 *         input, or @c ops_allow(@c PORT_OPS_LOOKUP) denied (deny → put + NULL;
 *         errno discarded).
 * @note Caller @c ref_put(..., free_named_message_port_ref).
 */
Named_Message_Port_t* port_table_lookup(struct Port_Table* table,
                                        const char* name);

/**
 * @brief Look up a port by name and optionally capture a cache token.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @param tok_out Optional output for a stable (index, gen) token; may be NULL.
 * @return Named port with refcount incremented, or NULL if not found, invalid
 *         input, or @c ops_allow(@c PORT_OPS_LOOKUP) denied (same as
 *         @c port_table_lookup).
 */
Named_Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                                   const char* name,
                                                   name_index_token_t* tok_out);

/**
 * @brief Resolve a cached token to a named port under the table lock.
 * @param table Port table to search.
 * @param tok Cached token from a prior lookup; if NULL, behaves like
 *        @c port_table_lookup.
 * @param name Port name used to validate the token.
 * @return Named port with refcount incremented, or NULL if the token is stale,
 *         input is invalid, or @c ops_allow(@c PORT_OPS_LOOKUP) denied
 *         (re-checked on every resolve).
 */
Named_Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                               const name_index_token_t* tok,
                                               const char* name);

/**
 * @brief Test whether @p port is still the live registered entry for @p name.
 * @param table Port table to search.
 * @param name Port name to compare.
 * @param port Named port pointer to validate.
 * @return true if @p port matches the registered entry for @p name; false
 *         otherwise.
 */
bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Named_Message_Port_t* port);

/**
 * @brief Register a named port in the table under its name.
 * @param table Port table to update.
 * @param port Port to register; must have a non-empty name; embedded life must
 *        be @c PORT_OPS_LIFE_OPEN (CLOSED/CLOSING → @c -E_REND_PORT_CLOSED).
 * @return @c REND_SUCCESS on success or if @p port is already the registered
 *         entry for that name; @c -E_IN_PARAM on invalid input;
 *         @c -E_REND_PORT_CLOSED if not OPEN; @c -E_RENDEZVOS if the name is
 *         taken / register fails / ref_get fails; or the @c error_t from
 *         @c ops_allow(@c PORT_OPS_REGISTER) if denied.
 *
 * @note Runs under the table MCS lock. On success: @c port->table is set and
 *       the table holds one ref. Visibility to lookup begins only after this
 *       returns (create alone is not discoverable).
 * @note @c PORT_OPS_REGISTER @c ops_allow (if any) runs under that lock.
 */
error_t register_named_port(struct Port_Table* table,
                            Named_Message_Port_t* port);

/**
 * @brief Close the embedded rendezvous, remove the name from the table, drop
 *        the table's ref.
 * @param table Port table to update.
 * @param name Port name to unregister.
 * @return @c REND_SUCCESS (including name already absent / not in table);
 *         @c -E_IN_PARAM if @p table or @p name is NULL; close failure from
 *         @c named_port_ensure_closed / @c close_port if ensure fails (entry
 *         left in the table; table pin not dropped).
 *
 * @note Order: hold a temporary ref → @c close_port outside the table MCS
 *       (may @c schedule) → under lock leave the directory → unlock → drop
 *       the table pin (and the temporary hold). Close-before-leave means
 *       @c register_named_port cannot reclaim the same object (not OPEN);
 *       lookups may briefly observe a CLOSED entry until leave completes.
 * @note Close-wake is asymmetric (implemented in @c close_port /
 *       @c port_clean_thread_queue):
 *       - blocked senders: always @c THREAD_FLAG_IPC_PORT_CLOSED →
 *         @c send_msg returns @c -E_REND_PORT_CLOSED (orphan send msg dropped);
 *       - blocked receivers: preferably deliver @c KMSG_OP_SYSTEM_PORT_CLOSED
 *         so @c recv_msg returns @c REND_SUCCESS and the caller dequeues that
 *         kmsg; otherwise OR the same flag → @c -E_REND_PORT_CLOSED.
 */
error_t unregister_named_port(struct Port_Table* table, const char* name);

/** Boot-time global port table pointer (set by @c global_port_init). */
extern struct Port_Table* global_port_table;

/**
 * @brief Create and initialize the global port table at boot.
 * @return @c REND_SUCCESS on success; @c -E_RENDEZVOS on allocation failure.
 *
 * @note Sets @c global_port_table to an empty table so later init can
 *       @c register_named_port. Does not register any ports itself.
 */
error_t global_port_init(void);

/* ---- Named IPC (ops_allow then anon send/recv) ----
 *
 * Same enqueue / dequeue contract as @c send_msg / @c recv_msg /
 * @c ipc_try_send_msg / @c ipc_try_recv_msg. Admission deny returns the
 * @c ops_allow error without entering anon ipc (message stays enqueued on
 * send paths). @c ops_allow NULL → allow. Allow is not a separate public
 * entry — only these wrappers (and lookup/register) invoke it.
 */

/** Blocking send after @c PORT_OPS_SEND allow. */
error_t named_port_send_msg(Named_Message_Port_t* port);

/** Non-blocking send after @c PORT_OPS_SEND allow. */
error_t named_port_try_send_msg(Named_Message_Port_t* port);

/** Blocking recv after @c PORT_OPS_RECV allow. */
error_t named_port_recv_msg(Named_Message_Port_t* port);

/** Non-blocking recv after @c PORT_OPS_RECV allow. */
error_t named_port_try_recv_msg(Named_Message_Port_t* port);

#endif /* _RENDEZVOS_NAMED_PORT_H_ */
