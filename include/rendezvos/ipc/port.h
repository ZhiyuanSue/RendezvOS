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

#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

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
 * ACTIVE:     created, not in name_index, ops must not begin)
 * REGISTERED: in name_index, port_ops_* can begin/end
 * CLOSING:    unregister or register_abort,ops must not begin
 * CLOSED:     all the msqueue request are cleaned
 */
#define PORT_OPS_LIFE_CLOSED     0
#define PORT_OPS_LIFE_ACTIVE     1
#define PORT_OPS_LIFE_REGISTERED 2
#define PORT_OPS_LIFE_CLOSING    3

#ifndef PORT_SLOTS_INITIAL_CAP
#define PORT_SLOTS_INITIAL_CAP (32ULL)
#endif
#ifndef PORT_HT_INITIAL_CAP
#define PORT_HT_INITIAL_CAP (64ULL)
#endif

/*port structure*/
typedef struct Msg_Port Message_Port_t;

/**
 * @brief Optional hook on first port allocation (before @c register_port).
 * @param port Port to initialize; append bytes at @c port->append_port_info when
 *        @c append_info_len is non-zero.
 */
typedef error_t (*port_append_init_t)(Message_Port_t* port);

/**
 * @brief Optional hook before port memory is freed (@c delete_message_port_structure).
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
 *        @c PORT_OPS_SEND, @c PORT_OPS_RECV, and @c PORT_OPS_REGISTER.
 * @return @c REND_SUCCESS to allow; any other @c error_t denies (lookup → NULL;
 *         send/recv → @c port_ops_begin fails; register → error from
 *         @c register_port).
 */
typedef error_t (*port_ops_allow_t)(Message_Port_t* port,
                                    enum port_ops_type op_type,
                                    const char* lookup_name);

/**
 * @brief Port append lifecycle + IPC admission gates.
 *
 * Stored as a pointer on each port; upper layers usually pass one static table.
 * @c ops_allow and @c init/@c fini may be NULL (allow / no-op).
 */
typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;
struct Msg_Port {
        ms_queue_t thread_queue; /* thread wait queue */
        ref_count_t refcount; /* port refcount */
        struct Port_Table* table; /* belonging register table（if registered）
                                   */
        char name[PORT_NAME_LEN_MAX]; /* port name */
        /*
         * Service id bound to this port name.
         * Used as kmsg_hdr.module for fast "is this for me?" validation.
         * Routing and discovery still use the port name string.
         */
        u16 service_id;
        atomic64_t ops_life; /* PORT_OPS_LIFE_* status */
        atomic64_t ops_count; /* count for how much the receiver/sender are
                                 operating */
        const struct port_append_hooks* append_hooks;
        char append_port_info[];
};

/* ---- ops basic funcs ---- */

/** Create-time init only: life starts as ACTIVE. */
static inline void port_ops_life_init(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_init(&port->ops_life, PORT_OPS_LIFE_ACTIVE);
}

static inline i64 port_ops_life_get(const Message_Port_t* port)
{
        if (!port)
                return PORT_OPS_LIFE_CLOSED;
        return (i64)atomic64_load((volatile const u64*)&port->ops_life.counter);
}

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

static inline bool port_is_registered(const Message_Port_t* port)
{
        return port_ops_life_get(port) == PORT_OPS_LIFE_REGISTERED;
}

static inline void port_ops_count_init(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_init(&port->ops_count, 0);
}

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

static inline size_t port_append_info_len(const Message_Port_t* port)
{
        return (port && port->append_hooks) ?
                       port->append_hooks->append_info_len :
                       0;
}

static inline size_t message_port_total_size(const port_append_hooks_t* hooks)
{
        size_t n = sizeof(Message_Port_t);

        if (hooks && hooks->append_info_len)
                n += hooks->append_info_len;
        return n;
}
/**
 * @brief Enter a send/recv/try critical section on @p port (lifecycle gate +
 *        @c ops_allow for @p op_type).
 * @param op_type @c PORT_OPS_SEND or @c PORT_OPS_RECV only.
 * @return true if entered; false if port is closing/closed or @c ops_allow
 *         denied (caller returns @c -E_REND_PORT_CLOSED). Pair with
 *         @c port_ops_end before @c schedule() on the blocking wait path.
 */
bool port_ops_begin(Message_Port_t* port, enum port_ops_type op_type);

/**
 * @brief Leave the critical section started by port_ops_begin.
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
 * @return One of IPC_PORT_STATE_EMPTY, IPC_PORT_STATE_SEND, or
 *         IPC_PORT_STATE_RECV.
 */
static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

/**
 * @brief Allocate and initialize an unregistered message port.
 * @param name Port name / name_index key (non-empty, shorter than
 * PORT_NAME_LEN_MAX).
 * @param hooks Optional append table; NULL for no append and all gates open.
 * @return New port with refcount 1, or NULL on invalid name, init failure, or
 *         allocation failure.
 */
Message_Port_t* create_message_port(const char* name,
                                    const port_append_hooks_t* hooks);

/**
 * @brief Free port memory after the wait queue has been drained.
 * @param port Port to destroy; no-op if NULL.
 */
void delete_message_port_structure(Message_Port_t* port);

/**
 * @brief Refcount destructor for Message_Port_t (calls
 * delete_message_port_structure).
 * @param ref_count_ptr Pointer to port->refcount.
 * @return REND_SUCCESS on success; -E_IN_PARAM if ref_count_ptr is NULL.
 */
error_t free_message_port_ref(ref_count_t* ref_count_ptr);

/**
 * @brief Allocate a port table and initialize its name index.
 * @return New Port_Table, or NULL on allocation failure.
 */
struct Port_Table* port_table_create(void);

/**
 * @brief Initialize an existing Port_Table name index.
 * @param table Table to initialize; no-op if NULL.
 */
void port_table_init(struct Port_Table* table);

/**
 * @brief Look up a registered port by name and hold a reference.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @return Port with refcount incremented, or NULL if not found or on invalid
 *         input.
 */
Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name);

/**
 * @brief Look up a port by name and optionally capture a cache token.
 * @param table Port table to search.
 * @param name Port name to look up.
 * @param tok_out Optional output for a stable (index, gen) token; may be NULL.
 * @return Port with refcount incremented, or NULL if not found or on invalid
 *         input.
 */
Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                             const char* name,
                                             name_index_token_t* tok_out);

/**
 * @brief Resolve a cached token to a port under the table lock.
 * @param table Port table to search.
 * @param tok Cached token from a prior lookup; if NULL, behaves like
 *        port_table_lookup.
 * @param name Port name used to validate the token.
 * @return Port with refcount incremented, or NULL if the token is stale or
 *         input is invalid.
 */
Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const name_index_token_t* tok,
                                         const char* name);

/**
 * @brief Test whether port is still the live registered entry for name.
 * @param table Port table to search.
 * @param name Port name to compare.
 * @param port Port pointer to validate.
 * @return true if port matches the registered entry for name; false otherwise.
 */
bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Message_Port_t* port);

/**
 * @brief Register a port in the table under its name.
 * @param table Port table to update.
 * @param port Port to register; must have a non-empty name.
 * @return REND_SUCCESS on success or if port is already registered in table;
 *         -E_IN_PARAM on invalid input; -E_RENDEZVOS if the name is taken or
 *         registration fails.
 */
error_t register_port(struct Port_Table* table, Message_Port_t* port);

/**
 * @brief Remove a port from the table and drop the table's reference.
 * @param table Port table to update.
 * @param name Port name to unregister.
 * @return REND_SUCCESS; -E_IN_PARAM if table or name is NULL.
 */
error_t unregister_port(struct Port_Table* table, const char* name);

/**
 * @brief Tear down a port table and free its backing storage.
 * @param table Table to destroy; no-op if NULL.
 */
void delete_port_table_structure(struct Port_Table* table);

/* Global port table - declared in port.c */
extern struct Port_Table* global_port_table;

/**
 * @brief Create and initialize the global port table at boot.
 * @return REND_SUCCESS on success; -E_RENDEZVOS if allocation fails.
 */
error_t global_port_init(void);

#endif
