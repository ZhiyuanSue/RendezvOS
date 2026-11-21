#ifndef _RENDEZVOS_PORT_H_
#define _RENDEZVOS_PORT_H_

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/ms_queue.h>
#include <common/refcount.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/error.h>

/**
 * Tag bits in the port @c thread_queue tagged pointer for empty / send / recv.
 */
#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

/*
 * DONOT CHANGE THE FOLLOWING COMMENT!
 * Port have a life cycle, and if it's closing, it should not allow the
 * send/recv Even there might have some reference. So we add the following port
 * ops micro and the port_ops_* functions. The name means whether the port can
 * do any ops.
 *
 * It can be seen as a rw lock, the lock between recv/send or recv/recv or
 * send/send should not be locked, and they using lock free algorithms to work.
 *
 * And the lock between recv/send thread and the thread try to close
 * port(closing thread) should be work.
 *
 * If there have some recv/send threads, they must using
 * port_ops_begin/port_ops_end to protect it, and the closing thread should
 * not clean the thread_queue.
 *
 * And if the closing thread have get the 'lock',
 * the send/recv must fail and return the -E_REND_PORT_CLOSED
 *
 * You have to consider the schedule, and should not hold the 'lock' when
 * blocked
 */

/*
 * Port lifecycle (ops_life):
 *   OPEN     create / message_port_init; begin may succeed
 *   CLOSING  close in progress; begin must fail
 *   CLOSED   thread_queue cleaned; terminal
 * Transitions: init/create → OPEN; close_port OPEN→CLOSING → clean → CLOSED.
 * close_port does not drop refcount; caller ref_puts after close as needed.
 */
#define PORT_OPS_LIFE_CLOSED  0
#define PORT_OPS_LIFE_OPEN    1
#define PORT_OPS_LIFE_CLOSING 2

typedef struct Msg_Port Message_Port_t;

#define PORT_COMMON                                                         \
        ms_queue_t thread_queue; /* Blocked senders or receivers. */        \
        ms_queue_node_t msq_dummy; /* Initial MSQ dummy; not heap-freed. */ \
        ref_count_t refcount; /* Port refcount */                           \
        u32 service_id; /* Kmsg / service id for this port. */              \
        atomic64_t ops_life; /* @c PORT_OPS_LIFE_* status */                \
        /* In-flight send/recv (@c port_ops_begin/@c port_ops_end). Gates   \
         * close drain only — does not serialize concurrent send/send. */ \
        atomic64_t ops_count;                                               \
        /* Optional owner-list node (@c create_* owner_list). Any list head; \
         * often @c Thread_Base.owned_*. @c close_port does not unlink. */  \
        struct list_entry owner_link

/**
 * @brief Message port: anonymous rendezvous point with a lock-free wait queue.
 */
struct Msg_Port {
        PORT_COMMON;
};

/* ---- ops basic funcs ---- */

/** Create-time init only: life starts as OPEN. */
static inline void port_ops_life_init(Message_Port_t* port)
{
        if (!port)
                return;
        atomic64_init(&port->ops_life, PORT_OPS_LIFE_OPEN);
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

/* True if life is @c PORT_OPS_LIFE_OPEN . */
static inline bool port_is_open(const Message_Port_t* port)
{
        return port_ops_life_get(port) == PORT_OPS_LIFE_OPEN;
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

/**
 * @brief Enter a send/recv/try critical section on @p port.
 *
 * Requires life @c PORT_OPS_LIFE_OPEN , then increments @c ops_count and
 * re-checks still OPEN (else undoes and fails).
 *
 * @return true if entered; false if not open / closing. Callers fold false to
 * @c -E_REND_PORT_CLOSED.
 * @note Pair with @c port_ops_end. On the blocking wait path call
 *       @c port_ops_end **before** @c schedule so @c close_port can drain
 *       @c ops_count==0. @c ops_count does **not** serialize concurrent
 *       send/send — only gates against close.
 */
bool port_ops_begin(Message_Port_t* port);

/**
 * @brief Leave the critical section started by @c port_ops_begin (@c
 * ops_count--).
 */
void port_ops_end(Message_Port_t* port);

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
 * @brief In-place init (embeddable or after heap alloc).
 * @param port Storage for the port; must be fresh (e.g. zeroed). Do not re-init
 *        a live port.
 * @param service_id Kmsg / service id for this port.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p port is NULL.
 * @note Sets @c refcount to 1 (creator). @c close_port does **not** drop refs;
 *       after close the creator @c ref_put s as needed. Heap create path
 *       typically @c close_port then @c ref_put(..., free_message_port_ref) to
 *       @c m_free.
 * @note Does **not** link onto an owned list — only @c create_* with a
 *       non-NULL @p owner_list does.
 */
error_t message_port_init(Message_Port_t* port, u32 service_id);

/**
 * @brief Heap-allocate and @c message_port_init an anonymous port.
 * @param service_id Kmsg / service id for this port.
 * @param owner_list Optional owner list head (any @c list_entry, not limited
 *        to @c Thread_Base; often @c &thread->owned_anon_ports). NULL = no
 *        link. Still linked ⇔ create ref not yet dropped.
 * @return New port with refcount 1 and life @c OPEN, or NULL on alloc failure.
 */
Message_Port_t* create_message_port(u32 service_id,
                                    struct list_entry* owner_list);

/**
 * @brief Free a **standalone heap** @c Message_Port_t (defensive queue clean +
 *        @c m_free).
 * @param port Port to destroy; no-op if NULL. Must be from
 *        @c create_message_port / @c m_alloc(sizeof(Message_Port_t)).
 */
void delete_message_port_structure(Message_Port_t* port);

/**
 * @brief Refcount destructor for standalone heap @c Message_Port_t.
 * @param ref_count_ptr Pointer to @c port->refcount.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if @p ref_count_ptr is NULL.
 */
error_t free_message_port_ref(ref_count_t* ref_count_ptr);

/**
 * @brief Put the create / ownership ref from @c create_message_port.
 *
 * Unlinks @c owner_link, then @c ref_put s. Still linked ⇔ this ref not yet
 * put. Do **not** use for lookup pins (plain @c ref_put).
 */
static inline void message_port_put_owner_ref(Message_Port_t* port)
{
        if (!port)
                return;
        list_del_init(&port->owner_link);
        ref_put(&port->refcount, free_message_port_ref);
}

/**
 * @brief Close the port: OPEN→CLOSING, drain @c ops_count, clean waiters,
 *        CLOSING→CLOSED. Does **not** @c ref_put; caller drops refs afterward.
 * @param port Port to close.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM; @c -E_REND_PORT_CLOSED if not OPEN
 *         (OPEN→CLOSING CAS failed — already CLOSING/CLOSED); @c -E_RENDEZVOS
 *         if CLOSING→CLOSED CAS fails after queue clean.
 *
 * @note Close-wake is asymmetric (@c port_clean_thread_queue):
 *       - blocked senders: always @c THREAD_FLAG_IPC_PORT_CLOSED →
 *         @c send_msg returns @c -E_REND_PORT_CLOSED (orphan send msg dropped);
 *       - blocked receivers: preferably deliver @c KMSG_OP_SYSTEM_PORT_CLOSED
 *         so @c recv_msg returns @c REND_SUCCESS and the caller dequeues that
 *         kmsg; otherwise OR the same flag → @c -E_REND_PORT_CLOSED.
 * @note After close, @c message_port_put_owner_ref (or leave linked for exit
 *       phase2). Lookup pins: plain @c ref_put.
 * @note Does **not** unlink @c owner_link; exit phase1 closes/unregisters,
 *       phase2 @c message_port_put_owner_ref s leftovers. Last @c free_*_ref
 *       also @c list_del_init s if still linked.
 */
error_t close_port(Message_Port_t* port);

#endif
