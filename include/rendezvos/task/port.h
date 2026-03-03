#ifndef _RENDEZVOS_PORT_H_
#define _RENDEZVOS_PORT_H_

#include <common/dsa/ms_queue.h>
#include <common/dsa/rb_tree.h>
#include <common/refcount.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/error.h>

#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

#define THREAD_MAX_KNOWN_PORTS 32
#define PORT_CACHE_NAME_LEN   64
#define PORT_REGISTRY_NAME_MAX 64

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        ms_queue_t thread_queue;
};

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

/* Port registry structures (internal) */
struct port_registry_entry {
        struct rb_node rb_node;
        char* name;
        Message_Port_t* port;
        u16 version;
        ref_count_t refcount;
        bool registered;
        struct port_registry* registry;
};

struct port_registry {
        struct rb_root root;
        cas_lock_t lock;
        u16 version_counter;
};

/* Thread port cache */
struct thread_port_cache_entry {
        char name[PORT_CACHE_NAME_LEN];
        struct port_registry_entry* entry;
        u16 version;
        u16 lru_counter;  /* LRU: 计数越大表示越久未使用，0表示最近使用 */
};

struct thread_port_cache {
        struct thread_port_cache_entry entries[THREAD_MAX_KNOWN_PORTS];
        u32 count;
        cas_lock_t lock;
};

typedef struct Thread_Base Thread_Base;

/* Basic port operations */
Message_Port_t* create_message_port();
void delete_message_port(Message_Port_t* port);

/* Port discovery operations */
void port_discovery_init(void);
void thread_port_cache_init(struct thread_port_cache* cache);
void thread_port_cache_clear(struct thread_port_cache* cache);
error_t thread_register_port(Thread_Base* thread, const char* name);
error_t thread_unregister_port(Thread_Base* thread);
Message_Port_t* thread_lookup_port(const char* name);

#endif