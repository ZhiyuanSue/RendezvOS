#ifndef _RENDEZVOS_PORT_H_
#define _RENDEZVOS_PORT_H_

#include <common/types.h>
#include <common/dsa/ms_queue.h>
#include <common/limits.h>
#include <common/refcount.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/error.h>

#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0
#define IPC_PORT_STATE_SEND  1
#define IPC_PORT_STATE_RECV  2

/* Port table backend: dynamic slot array + open-addressing name hash. */

/** Invalid slot index for freelist (never a valid table index). */
#define PORT_TABLE_SLOT_FREE_INVALID U64_MAX
/* Invalid token slot index (never a valid slot index). */
#define PORT_TABLE_SLOT_INDEX_INVALID ((u32) - 1)

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
        bool registered; /* 是否已注册 */
};

struct port_slot {
        u64 gen;
        /* Use named constants PORT_SLOT_FREE / PORT_SLOT_USED for readability.
         */
        u64 used;
        /*
         * Slot storage (not an IPC message body). Interpretation:
         * used!=0 -> .port only
         * used==0 -> .next_free only (freelist)
         */
        union {
                Message_Port_t* port;
                u64 next_free;
        } storage;
};

#define PORT_SLOT_FREE (0ULL)
#define PORT_SLOT_USED (1ULL)

/*
 * Table sizes and indices are u64 on 64-bit targets: consistent with LP64,
 * no practical RAM for billions of slots, and i64 hash buckets (sentinel +
 * index).
 */
struct Port_Table {
        spin_lock lock; /* 保护整个表 */
        struct allocator* alloc; /* slot/ht 内存：与建表时 CPU 的 kallocator
                                    一致 */
        struct port_slot* slots;
        u64 slot_cap;
        u64 free_head; /* freelist head; PORT_TABLE_SLOT_FREE_INVALID if empty
                        */
        u64 live_ports; /* used slots count */
        i64* ht; /* open addressing: PORT_HT_EMPTY/TOMB, else slot index */
        u64 ht_cap; /* power of 2 */
        u64 ht_mask; /* ht_cap - 1 */
        u64 ht_tombs; /* how many buckets are currently marked PORT_HT_TOMB */
};

extern struct spin_lock_t port_table_spin_lock;

static inline u16 ipc_get_queue_state(Message_Port_t* port)
{
        tagged_ptr_t tail = atomic64_load(&port->thread_queue.tail);
        return tp_get_tag(tail) & ((1 << IPC_PORT_APPEND_BITS) - 1);
}

typedef struct {
        u32 slot_index;
        u16 slot_gen;
} port_table_slot_token_t;

static inline void port_table_slot_token_invalidate(port_table_slot_token_t* t)
{
        t->slot_index = PORT_TABLE_SLOT_INDEX_INVALID;
        t->slot_gen = 0;
}

/* Basic port operations */
Message_Port_t* create_message_port(const char* name);
void delete_message_port_structure(Message_Port_t* port);
void free_message_port_ref(ref_count_t* ref_count_ptr);

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
                                             port_table_slot_token_t* tok_out);
/*
 * Resolve a cached token under the table lock; on success returns with the same
 * ref semantics as port_table_lookup. If `tok` is NULL, behaves like
 * port_table_lookup.
 */
Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const port_table_slot_token_t* tok,
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