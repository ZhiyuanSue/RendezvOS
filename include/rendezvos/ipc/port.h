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

#ifndef PORT_SLOTS_INITIAL_CAP
#define PORT_SLOTS_INITIAL_CAP (32ULL)
#endif
#ifndef PORT_HT_INITIAL_CAP
#define PORT_HT_INITIAL_CAP (64ULL)
#endif

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t thread_queue; /* 线程等待队列 */
        ref_count_t refcount; /* 引用计数 */
        struct Port_Table* table; /* 所属注册表（如果已注册） */
        char name[PORT_NAME_LEN_MAX]; /* 端口名称 */
        /*
         * Service id bound to this port name.
         * Used as kmsg_hdr.module for fast "is this for me?" validation.
         * Routing and discovery still use the port name string.
         */
        u16 service_id;
        bool registered; /* 是否已注册 */
};

/* Global port table: string-keyed index over Message_Port_t (see name_index).
 */
struct Port_Table {
        name_index_t by_name;
};

extern struct spin_lock_t port_table_spin_lock;

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

/* Basic port operations */
Message_Port_t* create_message_port(const char* name);
void delete_message_port_structure(Message_Port_t* port);
error_t free_message_port_ref(ref_count_t* ref_count_ptr);

/* Port table operations */
struct Port_Table* port_table_create(void);
void port_table_init(struct Port_Table* table);
Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name);
/*
 * Like port_table_lookup, but optionally fills *tok_out with stable (index,gen)
 * for per-thread cache. tok_out may be NULL.
 */
Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                             const char* name,
                                             name_index_token_t* tok_out);
/*
 * Resolve a cached token under the table lock; on success returns with the same
 * ref semantics as port_table_lookup. If `tok` is NULL, behaves like
 * port_table_lookup.
 */
Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const name_index_token_t* tok,
                                         const char* name);
bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Message_Port_t* port);
error_t register_port(struct Port_Table* table, Message_Port_t* port);
error_t unregister_port(struct Port_Table* table, const char* name);
void delete_port_table_structure(struct Port_Table* table);

/* Global port table - declared in port.c */
extern struct Port_Table* global_port_table;
error_t global_port_init(void);

#endif