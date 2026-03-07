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

#define PORT_NAME_LEN_MAX 64

/*port structure*/
typedef struct Msg_Port Message_Port_t;
struct Msg_Port {
        struct rb_node rb_node; /* 用于插入到port_table的红黑树 */
        ms_queue_t thread_queue; /* 线程等待队列 */
        ref_count_t refcount; /* 引用计数 */
        struct Port_Table* table; /* 所属注册表（如果已注册） */
        char name[PORT_NAME_LEN_MAX]; /* 端口名称 */
        bool registered; /* 是否已注册 */
};

struct Port_Table {
        struct rb_root root; /* 红黑树根节点 */
        spin_lock lock; /* 保护整个表 */
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
void free_message_port_ref(ref_count_t* ref_count_ptr);

/* Port table operations */
struct Port_Table* port_table_create(void);
void port_table_init(struct Port_Table* table);
Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name);
error_t register_port(struct Port_Table* table, Message_Port_t* port);
error_t unregister_port(struct Port_Table* table, const char* name);
void delete_port_table_structure(struct Port_Table* table);

/* Global port table - declared in port.c */
extern struct Port_Table* global_port_table;
error_t global_port_init(void);

#endif