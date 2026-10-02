#ifndef _RENDEZVOS_PORT_H_
#define _RENDEZVOS_PORT_H_

#include <common/types.h>
#include <common/dsa/ms_queue.h>
#include <common/limits.h>
#include <common/refcount.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/error.h>
#include <rendezvos/registry/name_index.h>

/**
 * Tag bits in the port @c thread_queue tagged pointer for empty / send / recv.
 */
#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

/** Max port name length including trailing NUL. */
#define PORT_NAME_LEN_MAX 64

/*
 * DONOT CHANGE THE FOLLOWING COMMENT!
 * Port have a life cycle, and if it's unregisted, it should not allow the
 * send/recv Even there might have some reference So we add the following port
 * ops micro and the port_ops_* functions. The name means whether the port can
 * do any ops.
 *
 * It can be seen as a rw lock, the lock between recv/send or recv/recv or
 * send/send should not be locked, and they using lock free algorithms to work.
 *
 * And the lock between recv/send thread and the unregister thread should be
 * work.
 *
 * If there have some recv/send threads, they must using
 * port_ops_begin/port_ops_end to protect it, and the unregister thread should
 * not clean the thread_queue.
 *
 * And if the unregister thread have get the lock,
 * the send/recv must fail and return the -E_REND_PORT_CLOSED
 *
 * You have to consider the schedule, and should not hold the 'lock' when
 * blocked
 */

/*
 * Port lifecycle (ops_life) — not to be confused with port_ops_type gates:
 *   ACTIVE     created, not in name_index; port_ops_begin must fail
 *   REGISTERED in name_index; begin may succeed
 *   CLOSING    unregister in progress; begin must fail
 *   CLOSED     thread_queue cleaned; terminal
 * Transitions: create→ACTIVE; register on_register ACTIVE→REGISTERED;
 * unregister on_unregister REGISTERED→CLOSING then clean→CLOSED.
 */
#define PORT_OPS_LIFE_CLOSED     0
#define PORT_OPS_LIFE_ACTIVE     1
#define PORT_OPS_LIFE_REGISTERED 2
#define PORT_OPS_LIFE_CLOSING    3

/* Default initial slot capacity for a new port table name index. */
#ifndef PORT_SLOTS_INITIAL_CAP
#define PORT_SLOTS_INITIAL_CAP (32ULL)
#endif
/* Default initial hash-table capacity for a new port table name index. */
#ifndef PORT_HT_INITIAL_CAP
#define PORT_HT_INITIAL_CAP (64ULL)
#endif

typedef struct Msg_Port Message_Port_t;

/**
 * @brief Optional hook on first port allocation (before @c register_port).
 * @param port Port to initialize; append bytes at @c port->append_port_info when
 *        @c append_info_len is non-zero.
 * @return @c REND_SUCCESS on success; any other @c error_t aborts create (alloc
 *         rolled back).
 */
typedef error_t (*port_append_init_t)(Message_Port_t* port);

/**
 * @brief Optional hook before port memory is freed
 *        (@c delete_message_port_structure).
 * @param port Port being destroyed.
 */
typedef void (*port_append_fini_t)(Message_Port_t* port);

/**
 * @brief Gate operation kinds for @c port_ops_allow_t / @c port_ops_begin.
 *
 * Not to be confused with @c PORT_OPS_LIFE_* (port lifecycle).
 */
enum port_ops_type {
        PORT_OPS_LOOKUP,
        PORT_OPS_SEND,
        PORT_OPS_RECV,
        PORT_OPS_REGISTER,
};

/**
 * @brief Optional admission gate (NULL = all allow).
 *
 * Invoked on the **accessor CPU context**: policy must use
 * @c get_cpu_current_thread() (and thread append) to identify who is acting.
 * Core does not pass a separate actor and does not implement namespace/capability
 * tables; the @c name index key is whatever string was registered.
 *
 * @param port Target port (always the port being looked up, sent to, received
 *        on, or registered).
 * @param op_type Which gate fired.
 * @param lookup_name Index key string for @c PORT_OPS_LOOKUP only; **NULL** for
 *        @c PORT_OPS_SEND, @c PORT_OPS_RECV, and @c PORT_OPS_REGISTER
 *        (REGISTER does not receive the port name string).
 * @return @c REND_SUCCESS to allow; any other @c error_t denies:
 *         LOOKUP → put + NULL (errno discarded);
 *         SEND/RECV → @c port_ops_begin returns false → ipc folds to
 *         @c -E_REND_PORT_CLOSED (indistinguishable from true close);
 *         REGISTER → that @c error_t from @c register_port.
 *
 * @note @c PORT_OPS_REGISTER runs **under the port-table MCS lock** — must not
 *       re-enter @c register_port / @c port_table_lookup on the same table
 *       (deadlock). Keep the callback fast; do not @c schedule.
 * @note Deny ≠ unregister: port stays REGISTERED; waiters already past begin
 *       are not re-checked on wake.
 */
typedef error_t (*port_ops_allow_t)(Message_Port_t* port,
                                    enum port_ops_type op_type,
                                    const char* lookup_name);

/**
 * @brief Port append lifecycle + IPC admission gates.
 *
 * Stored as a pointer on each port; upper layers usually pass one static table.
 * @c ops_allow and @c init/@c fini may be NULL (allow / no-op).
 * @c append_info_len bytes follow the struct as FAM (@c append_port_info).
 */
typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;

/**
 * @brief Message port: named rendezvous point with a lock-free wait queue.
 */
struct Msg_Port {
        ms_queue_t thread_queue; /* Blocked senders or receivers. */
        ref_count_t refcount; /* Port refcount */
        struct Port_Table* table; /* Owning table if registered; else NULL. */
        char name[PORT_NAME_LEN_MAX]; /* Port name / name_index key. */
        /**
         * Service id bound to this port name.
         * Used as @c kmsg_hdr.module for fast "is this for me?" validation.
         * Routing and discovery still use the port name string.
         */
        u16 service_id;
        atomic64_t ops_life; /* @c PORT_OPS_LIFE_* status */
        /**
         * Count of in-flight send/recv critical sections (@c port_ops_begin /
         * @c port_ops_end). Gates unregister drain only — does not serialize
         * concurrent send/send or recv/recv.
         */
        atomic64_t ops_count;
        const struct port_append_hooks* append_hooks;
        char append_port_info[]; /* FAM; length @c append_hooks->append_info_len. */
};

/* ---- ops basic funcs ---- */

/** Create-time init only: life starts as ACTIVE. */
static inline void port_ops_life_init(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_init(&port->ops_life, PORT_OPS_LIFE_ACTIVE);
}

/** Read @c ops_life ; NULL port return @c PORT_OPS_LIFE_CLOSED . */
static inline i64 port_ops_life_get(const Message_Port_t* port)
{
        if (!port)
                return PORT_OPS_LIFE_CLOSED;
        return (i64)atomic64_load((volatile const u64*)&port->ops_life.counter);
}

/**
 * @brief CAS @c ops_life from @p expect to @p target.
 * @return true if the CAS succeeded.
 */
static inline bool port_ops_set_life_with_expect(Message_Port_t* port,
                                                 i64 expect, i64 target)
{
        if (!port)
                return false;
        return atomic64_cas((volatile u64*)&port->ops_life.counter,
                            (u64)expect,
                            (u64)target)
               == (u64)expect;
}

/* True if life is @c PORT_OPS_LIFE_REGISTERED . */
static inline bool port_is_registered(const Message_Port_t* port)
{
        return port_ops_life_get(port) == PORT_OPS_LIFE_REGISTERED;
}

/* Create-time init: @c ops_count starts at 0 */
static inline void port_ops_count_init(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_init(&port->ops_count, 0);
}

/* Read @c ops_count; NULL port return 0 */
static inline i64 port_ops_count_get(const Message_Port_t* port)
{
        if (!port)
                return 0;
        return (i64)atomic64_load(
                (volatile const u64*)&port->ops_count.counter);
}

static inline void port_ops_count_inc(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_inc(&port->ops_count);
}

static inline void port_ops_count_dec(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_dec(&port->ops_count);
}

/* Append info length from hooks, or 0. */
static inline size_t port_append_info_len(const Message_Port_t* port)
{
        return (port && port->append_hooks) ?
                       port->append_hooks->append_info_len :
                       0;
}

/* Bytes to allocate for a port including optional append info. */
static inline size_t message_port_total_size(const port_append_hooks_t* hooks)
{
        size_t n = sizeof(Message_Port_t);

        if (hooks && hooks->append_info_len)
                n += hooks->append_info_len;
        return n;
}
/**
 * @brief Enter a send/recv/try critical section on @p port.
 *
 * Requires @p op_type of @c PORT_OPS_SEND or @c PORT_OPS_RECV, life
 * @c PORT_OPS_LIFE_REGISTERED, and optional @c ops_allow success; then bumps
 * @c ops_count and re-checks still REGISTERED (else undoes and fails).
 *
 * @return true if entered; false if wrong op_type, not registered / closing,
 *         or @c ops_allow denied. Callers fold false to @c -E_REND_PORT_CLOSED.
 * @note Pair with @c port_ops_end. On the blocking wait path call
 *       @c port_ops_end **before** @c schedule so unregister can drain
 *       @c ops_count==0. @c ops_count does **not** serialize concurrent
 *       send/send — only gates against unregister.
 */
bool port_ops_begin(Message_Port_t* port, enum port_ops_type op_type);

/**
 * @brief Leave the critical section started by @c port_ops_begin (@c ops_count--).
 */
void port_ops_end(Message_Port_t* port);

/* Global port table: string-keyed index over Message_Port_t (see name_index).
 */
struct Port_Table {
        name_index_t by_name;
};

extern struct spin_lock_t port_table_spin_lock;

/**
 * @brief Read the port thread-queue state tag (empty, send, or recv).
 * @param port Port whose queue state is queried.
 * @return One of @c IPC_PORT_STATE_EMPTY, @c IPC_PORT_STATE_SEND, or
 *         @c IPC_PORT_STATE_RECV.
 */
static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

/**
 * @brief Allocate and initialize an unregistered message port.
 * @param name Port name / name_index key (non-empty, shorter than
 *        @c PORT_NAME_LEN_MAX).
 * @param hooks Optional append table; NULL for no append and all gates open.
 * @return New port with refcount 1 and life @c ACTIVE, or NULL on invalid name,
 *         allocation failure, or @c hooks->init failure (rolls back alloc).
 * @note Sets @c service_id from the name (never 0). Does **not** register.
 */
Message_Port_t* create_message_port(const char* name,
                                    const port_append_hooks_t* hooks);

/**
 * @brief Free port memory after the wait queue has been drained.
 * @param port Port to destroy; no-op if NULL.
 */
void delete_message_port_structure(Message_Port_t* port);

/**
 * @brief Refcount destructor for @c Message_Port_t (calls
 *        @c delete_message_port_structure).
 * @param ref_count_ptr Pointer to @c port->refcount.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p ref_count_ptr is NULL.
 */
error_t free_message_port_ref(ref_count_t* ref_count_ptr);

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
 * @brief Look up a registered port by name and hold a reference.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @return Port with refcount incremented, or NULL if not found, invalid input,
 *         or @c ops_allow(@c PORT_OPS_LOOKUP) denied (deny → put + NULL;
 *         errno discarded).
 */
Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name);

/**
 * @brief Look up a port by name and optionally capture a cache token.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @param tok_out Optional output for a stable (index, gen) token; may be NULL.
 * @return Port with refcount incremented, or NULL if not found, invalid input,
 *         or @c ops_allow(@c PORT_OPS_LOOKUP) denied (same as
 *         @c port_table_lookup).
 */
Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                             const char* name,
                                             name_index_token_t* tok_out);

/**
 * @brief Resolve a cached token to a port under the table lock.
 * @param table Port table to search.
 * @param tok Cached token from a prior lookup; if NULL, behaves like
 *        @c port_table_lookup.
 * @param name Port name used to validate the token.
 * @return Port with refcount incremented, or NULL if the token is stale,
 *         input is invalid, or @c ops_allow(@c PORT_OPS_LOOKUP) denied
 *         (re-checked on every resolve).
 */
Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const name_index_token_t* tok,
                                         const char* name);

/**
 * @brief Test whether @p port is still the live registered entry for @p name.
 * @param table Port table to search.
 * @param name Port name to compare.
 * @param port Port pointer to validate.
 * @return true if @p port matches the registered entry for @p name; false
 *         otherwise.
 */
bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Message_Port_t* port);

/**
 * @brief Register a port in the table under its name.
 * @param table Port table to update.
 * @param port Port to register; must have a non-empty name; life should be
 *        @c PORT_OPS_LIFE_ACTIVE (create-time).
 * @return @c REND_SUCCESS on success or if @p port is already the registered
 *         entry for that name; @c -E_IN_PARAM on invalid input; @c -E_RENDEZVOS
 *         if the name is taken / register fails / ref_get fails; or the
 *         @c error_t from @c ops_allow(@c PORT_OPS_REGISTER) if denied.
 *
 * @note Runs under the table MCS lock. On success: life becomes
 *       @c PORT_OPS_LIFE_REGISTERED, @c port->table is set, and the table holds
 *       one ref. @c PORT_OPS_REGISTER @c ops_allow (if any) runs under that lock.
 */
error_t register_port(struct Port_Table* table, Message_Port_t* port);

/**
 * @brief Remove a port from the table, drain waiters, drop the table's ref.
 * @param table Port table to update.
 * @param name Port name to unregister.
 * @return @c REND_SUCCESS (including name already absent / not REGISTERED);
 *         @c -E_IN_PARAM if @p table or @p name is NULL.
 *
 * @note Transitions REGISTERED→CLOSING, waits until @c ops_count==0, wakes
 *       waiters, then CLOSING→CLOSED and drops the table's ref. After leave
 *       REGISTERED, new @c port_ops_begin fails.
 * @note Close-wake is asymmetric:
 *       - blocked senders: always @c THREAD_FLAG_IPC_PORT_CLOSED →
 *         @c send_msg returns @c -E_REND_PORT_CLOSED (orphan send msg dropped);
 *       - blocked receivers: preferably deliver @c KMSG_OP_SYSTEM_PORT_CLOSED
 *         so @c recv_msg returns @c REND_SUCCESS and the caller dequeues that
 *         kmsg; otherwise OR the same flag → @c -E_REND_PORT_CLOSED.
 */
error_t unregister_port(struct Port_Table* table, const char* name);

/**
 * @brief Tear down a port table and free its backing storage.
 * @param table Table to destroy; no-op if NULL.
 */
void delete_port_table_structure(struct Port_Table* table);

/** Boot-time global port table pointer (set by @c global_port_init) . */
extern struct Port_Table* global_port_table;

/**
 * @brief Create and initialize the global port table at boot.
 * @return @c REND_SUCCESS on success; @c -E_RENDEZVOS on allocation failure.
 *
 * @note Sets @c global_port_table to an empty table so later init can
 *       @c register_port. Does not register any ports itself.
 */
error_t global_port_init(void);

#endif
