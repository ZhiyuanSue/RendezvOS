#include <rendezvos/task/port.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/spin_lock.h>
#include <common/string.h>
#include <common/stddef.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(struct spin_lock_t, port_table_spin_lock);

Message_Port_t* create_message_port(const char* name)
{
        if (!name)
                return NULL;
        size_t name_len = strlen(name);
        if (name_len == 0 || name_len >= PORT_NAME_LEN_MAX)
                return NULL;

        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_Port_t* mp = (Message_Port_t*)cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Message_Port_t));
        if (!mp)
                return NULL;

        Ipc_Request_t* dummy_request_node =
                (Ipc_Request_t*)(cpu_kallocator->m_alloc(
                        cpu_kallocator, sizeof(Ipc_Request_t)));
        if (!dummy_request_node) {
                cpu_kallocator->m_free(cpu_kallocator, mp);
                return NULL;
        }

        memset(dummy_request_node, 0, sizeof(Ipc_Request_t));
        memcpy(mp->name, name, name_len + 1);
        msq_init(&mp->thread_queue, &dummy_request_node->ms_queue_node,
                 IPC_PORT_APPEND_BITS);
        ref_init(&mp->refcount); /* creator holds one ref */
        mp->table = NULL;
        mp->registered = false;
        mp->rb_node.left_child = mp->rb_node.right_child = NULL;
        mp->rb_node.black_height = 0;
        mp->rb_node.rb_parent_color = 0;

        return mp;
}

void delete_message_port_structure(Message_Port_t* port)
{
        if (!port)
                return;
        /*TODO: clean the queue*/
        percpu(kallocator)->m_free(percpu(kallocator), port);
}

void free_message_port_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Message_Port_t* port =
                container_of(ref_count_ptr, Message_Port_t, refcount);
        delete_message_port_structure(port);
}

struct Port_Table global_port_table;
bool global_port_table_initialized;

static Message_Port_t* port_table_search(struct Port_Table* table,
                                         const char* name)
{
        struct rb_node* node = table->root.rb_root;
        while (node) {
                Message_Port_t* port = container_of(node, Message_Port_t, rb_node);
                int cmp = strcmp(name, port->name);
                if (cmp < 0)
                        node = node->left_child;
                else if (cmp > 0)
                        node = node->right_child;
                else
                        return port;
        }
        return NULL;
}

struct Port_Table* port_table_create(void)
{
        struct allocator* a = percpu(kallocator);
        struct Port_Table* table =
                (struct Port_Table*)a->m_alloc(a, sizeof(struct Port_Table));
        if (table)
                port_table_init(table);
        return table;
}

void port_table_init(struct Port_Table* table)
{
        if (!table)
                return;
        table->root.rb_root = NULL;
        table->lock = NULL; /* MCS锁初始化为NULL */
}

void delete_port_table_structure(struct Port_Table* table)
{
        if (!table)
                return;
        /*TODO: 清理所有port？或者要求先unregister所有port */
        percpu(kallocator)->m_free(percpu(kallocator), table);
}

error_t register_port(struct Port_Table* table, Message_Port_t* port)
{
        if (!table || !port || !port->name[0])
                return -E_IN_PARAM;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* existing = port_table_search(table, port->name);
        if (existing) {
                /* 同名port已存在 */
                if (existing == port) {
                        /* 同一个port重复注册，直接返回成功 */
                        unlock_mcs(&table->lock, my_lock);
                        return REND_SUCCESS;
                }
                /* 不同的port同名，返回错误 */
                unlock_mcs(&table->lock, my_lock);
                return -E_RENDEZVOS;
        }

        /* 插入新port到树中 */
        port->rb_node.left_child = port->rb_node.right_child = NULL;
        port->rb_node.black_height = 0;
        port->rb_node.rb_parent_color = RB_RED;

        struct rb_node** new_link = &table->root.rb_root;
        struct rb_node* parent = NULL;
        while (*new_link) {
                parent = *new_link;
                Message_Port_t* pport =
                        container_of(parent, Message_Port_t, rb_node);
                int cmp = strcmp(port->name, pport->name);
                if (cmp < 0)
                        new_link = &parent->left_child;
                else
                        new_link = &parent->right_child;
        }
        RB_Link_Node(&port->rb_node, parent, new_link);
        RB_SolveDoubleRed(&port->rb_node, &table->root);

        port->table = table;
        port->registered = true;
        /* table holds one ref */
        if (!ref_get_not_zero(&port->refcount)) {
                /* port is being freed, cannot register */
                RB_Remove(&port->rb_node, &table->root);
                port->rb_node.left_child = port->rb_node.right_child = NULL;
                port->rb_node.black_height = 0;
                port->rb_node.rb_parent_color = 0;
                port->table = NULL;
                port->registered = false;
                unlock_mcs(&table->lock, my_lock);
                return -E_RENDEZVOS;
        }
        unlock_mcs(&table->lock, my_lock);
        return REND_SUCCESS;
}

error_t unregister_port(struct Port_Table* table, const char* name)
{
        if (!table || !name)
                return -E_IN_PARAM;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* port = port_table_search(table, name);
        if (!port || !port->registered) {
                /* port不存在或已unregister，幂等操作，返回成功 */
                unlock_mcs(&table->lock, my_lock);
                return REND_SUCCESS;
        }

        port->registered = false;
        RB_Remove(&port->rb_node, &table->root);
        port->rb_node.left_child = port->rb_node.right_child = NULL;
        port->rb_node.black_height = 0;
        port->rb_node.rb_parent_color = 0;
        port->table = NULL;
        unlock_mcs(&table->lock, my_lock);

        ref_put(&port->refcount, free_message_port_ref);
        return REND_SUCCESS;
}

Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name)
{
        if (!table || !name)
                return NULL;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* port = port_table_search(table, name);
        if (!port || !port->registered) {
                unlock_mcs(&table->lock, my_lock);
                return NULL;
        }
        if (!ref_get_not_zero(&port->refcount)) {
                unlock_mcs(&table->lock, my_lock);
                return NULL;
        }
        unlock_mcs(&table->lock, my_lock);
        return port;
}

void port_discovery_init(void)
{
        port_table_init(&global_port_table);
        global_port_table_initialized = true;
}

Message_Port_t* thread_lookup_port(const char* name)
{
        Thread_Base* self = get_cpu_current_thread();
        if (!self || !name || !global_port_table_initialized)
                return NULL;

        /* 1. 先查cache */
        Message_Port_t* port = thread_port_cache_lookup(self, name);
        if (port) {
                /* cache命中：cache已持有ref，调用者也需要ref */
                if (!ref_get_not_zero(&port->refcount)) {
                        /* port正在被释放 */
                        return NULL;
                }
                return port;
        }

        /* 2. cache未命中，从全局表查找 */
        port = port_table_lookup(&global_port_table, name);
        if (!port)
                return NULL;

        /* 3. 放入cache（cache_add会增加refcount给cache） */
        if (thread_port_cache_add(self, port) != REND_SUCCESS) {
                /* cache add失败，释放lookup时增加的ref */
                ref_put(&port->refcount, free_message_port_ref);
                return NULL;
        }
        /* 此时：lookup增加了1个ref（给调用者），cache_add增加了1个ref（给cache）
         * 调用者已经有了ref，直接返回即可 */
        return port;
}

error_t register_port_to_global(Message_Port_t* port)
{
        return register_port(&global_port_table, port);
}

error_t unregister_port_from_global(const char* name)
{
        return unregister_port(&global_port_table, name);
}

