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
        msq_init(&mp->thread_queue,
                 &dummy_request_node->ms_queue_node,
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

static Message_Port_t* port_table_search(struct Port_Table* table,
                                         const char* name)
{
        struct rb_node* node = table->root.rb_root;
        while (node) {
                Message_Port_t* port =
                        container_of(node, Message_Port_t, rb_node);
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

static void port_table_init_rb_node(Message_Port_t* port)
{
        port->rb_node.left_child = port->rb_node.right_child = NULL;
        port->rb_node.black_height = 0;
        port->rb_node.rb_parent_color = RB_RED;
}

static void port_table_insert_port(Message_Port_t* port,
                                   struct Port_Table* table)
{
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
}

static void port_table_remove_port(Message_Port_t* port,
                                   struct Port_Table* table)
{
        RB_Remove(&port->rb_node, &table->root);
        port->rb_node.left_child = port->rb_node.right_child = NULL;
        port->rb_node.black_height = 0;
        port->rb_node.rb_parent_color = 0;
        port->table = NULL;
        port->registered = false;
}

void free_message_port_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Message_Port_t* port =
                container_of(ref_count_ptr, Message_Port_t, refcount);

        if (port->registered && port->table) {
                struct Port_Table* table = port->table;
                struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
                lock_mcs(&table->lock, my_lock);

                if (port->registered && port->table == table) {
                        port_table_remove_port(port, table);
                }

                unlock_mcs(&table->lock, my_lock);
        }

        delete_message_port_structure(port);
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
        table->lock = NULL;
}

void delete_port_table_structure(struct Port_Table* table)
{
        if (!table)
                return;
        /*TODO: clean all the ports */
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
                if (existing == port) {
                        unlock_mcs(&table->lock, my_lock);
                        return REND_SUCCESS;
                }
                unlock_mcs(&table->lock, my_lock);
                return -E_RENDEZVOS;
        }

        port_table_init_rb_node(port);
        port_table_insert_port(port, table);
        /* table holds one ref */
        if (!ref_get_not_zero(&port->refcount)) {
                /* port is being freed, cannot register */
                port_table_remove_port(port, table);
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
                unlock_mcs(&table->lock, my_lock);
                return REND_SUCCESS;
        }

        port_table_remove_port(port, table);
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

struct Port_Table* global_port_table;
error_t global_port_init(void)
{
        global_port_table = port_table_create();
        if (!global_port_table) {
                pr_error("[ PORT ] Failed to create global port table\n");
                return -E_RENDEZVOS;
        }
        port_table_init(global_port_table);
        return REND_SUCCESS;
}
