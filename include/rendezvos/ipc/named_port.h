#ifndef _RENDEZVOS_NAMED_PORT_H_
#define _RENDEZVOS_NAMED_PORT_H_

#include <rendezvos/ipc/port.h>
#include <rendezvos/registry/name_index.h>

/*
 * Named ports: directory / discovery shell over an embedded anonymous
 * rendezvous (@c PORT_COMMON first).
 *
 * Layering:
 *   - Anon life (@c PORT_OPS_LIFE_* OPEN/CLOSING/CLOSED) lives in COMMON;
 *     @c close_port drains waiters. Directory membership is @c port->table.
 *   - Callers hold @c Named_Message_Port_t*. Pass @c named_port_as_anon() only
 *     into send/recv/close. Drop refs with @c free_named_message_port_ref —
 *     never @c free_message_port_ref / @c delete_message_port_structure.
 *   - Publish: create → register (visible to lookup). Unpublish:
 *     close embedded anon → leave directory → drop the table's pin.
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
 * Discovery admission only — **not** used by @c port_ops_begin / send / recv
 * (those gate solely on anon @c PORT_OPS_LIFE_OPEN).
 * Not to be confused with @c PORT_OPS_LIFE_* (anon rendezvous lifecycle).
 */
enum port_ops_type {
        PORT_OPS_LOOKUP,
        PORT_OPS_REGISTER,
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
 * @param port Target named port (looked up or being registered).
 * @param op_type Which gate fired.
 * @param lookup_name Index key for @c PORT_OPS_LOOKUP only; **NULL** for
 *        @c PORT_OPS_REGISTER (REGISTER does not receive the name string;
 *        use @c port->name).
 * @return @c REND_SUCCESS to allow; any other @c error_t denies:
 *         LOOKUP → drop hold + NULL (errno discarded);
 *         REGISTER → that @c error_t from @c register_named_port.
 *
 * @note @c PORT_OPS_REGISTER runs **under the port-table MCS lock** — must not
 *       re-enter @c register_named_port / @c port_table_lookup on the same
 *       table (deadlock). Keep the callback fast; do not @c schedule.
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

/**
 * @brief View the embedded anon rendezvous for IPC / @c close_port.
 * @warning Do not @c free_message_port_ref or @c delete_message_port_structure
 *          this pointer — it is not a standalone heap @c Message_Port_t.
 */
static inline Message_Port_t* named_port_as_anon(Named_Message_Port_t* named)
{
        return (Message_Port_t*)named;
}

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
 * Prefer already-CLOSED via @c unregister_named_port / explicit @c close_port.
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

#endif /* _RENDEZVOS_NAMED_PORT_H_ */
