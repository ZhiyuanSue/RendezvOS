#include <rendezvos/ipc/named_port.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/task/thread.h>
#include <common/string.h>
#include <common/dsa/list.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(struct spin_lock_t, port_table_spin_lock);

static error_t named_port_ops_allow(Named_Message_Port_t* port,
                                    enum port_ops_type op_type,
                                    const char* lookup_name)
{
        if (!port)
                return -E_IN_PARAM;

        switch (op_type) {
        case PORT_OPS_LOOKUP:
                break;
        case PORT_OPS_REGISTER:
        case PORT_OPS_SEND:
        case PORT_OPS_RECV:
                if (lookup_name)
                        return -E_IN_PARAM;
                break;
        default:
                return -E_IN_PARAM;
        }

        if (port->append_hooks && port->append_hooks->ops_allow)
                return port->append_hooks->ops_allow(
                        port, op_type, lookup_name);
        return REND_SUCCESS;
}

/*
 * name_index callbacks: @p value is the Named_Message_Port_t* stored in the
 * index (typed void* by the name_index API).
 */

static const char* named_port_index_get_name(void* value)
{
        Named_Message_Port_t* named_port = (Named_Message_Port_t*)value;

        return named_port ? named_port->name : NULL;
}

static bool named_port_index_hold(void* value)
{
        Named_Message_Port_t* named_port = (Named_Message_Port_t*)value;

        if (!named_port)
                return false;
        return ref_get_not_zero(&named_port->refcount);
}

static void named_port_index_drop(void* value)
{
        Named_Message_Port_t* named_port = (Named_Message_Port_t*)value;

        if (!named_port)
                return;
        ref_put(&named_port->refcount, free_named_message_port_ref);
}

static void named_port_index_on_register(void* value, void* owner_context)
{
        Named_Message_Port_t* named_port = (Named_Message_Port_t*)value;
        struct Port_Table* table = (struct Port_Table*)owner_context;

        if (!named_port)
                return;
        named_port->table = table;
}

static void named_port_index_on_unregister(void* value, void* owner_context)
{
        Named_Message_Port_t* named_port = (Named_Message_Port_t*)value;

        (void)owner_context;
        if (!named_port)
                return;
        named_port->table = NULL;
}

/*
 * Stable 32-bit service id derived from the port name (FNV-1a).
 * Used only for fast validation (kmsg_hdr.module); routing/discovery use the
 * port name string. Never returns 0.
 */
static u32 service_id_from_name(const char* name)
{
        u32 hash;
        u32 name_idx;

        if (!name || !name[0])
                return 0;
        hash = 2166136261u;
        for (name_idx = 0;
             name_idx < PORT_NAME_LEN_MAX && name[name_idx] != '\0';
             name_idx++) {
                hash ^= (u8)name[name_idx];
                hash *= 16777619u;
        }
        if (hash == 0)
                hash = 1;
        return hash;
}

Named_Message_Port_t*
create_named_message_port(const char* name, const port_append_hooks_t* hooks,
                          struct list_entry* owner_list)
{
        size_t name_len;
        struct allocator* cpu_kallocator;
        size_t port_bytes;
        Named_Message_Port_t* named_port;

        if (!name)
                return NULL;
        name_len = strlen(name);
        if (name_len == 0 || name_len >= PORT_NAME_LEN_MAX)
                return NULL;

        cpu_kallocator = percpu(kallocator);
        port_bytes = named_message_port_total_size(hooks);
        named_port = (Named_Message_Port_t*)cpu_kallocator->m_alloc(
                cpu_kallocator, port_bytes);
        if (!named_port)
                return NULL;

        if (message_port_init((Message_Port_t*)named_port,
                              service_id_from_name(name))
            != REND_SUCCESS) {
                cpu_kallocator->m_free(cpu_kallocator, named_port);
                return NULL;
        }

        memcpy(named_port->name, name, name_len + 1);
        named_port->table = NULL;
        named_port->append_hooks = hooks;

        if (hooks && hooks->init) {
                error_t e = hooks->init(named_port);

                if (e != REND_SUCCESS) {
                        cpu_kallocator->m_free(cpu_kallocator, named_port);
                        return NULL;
                }
        }

        if (owner_list)
                list_add_tail(&named_port->owner_link, owner_list);

        return named_port;
}

static error_t named_port_ensure_closed(Message_Port_t* anon)
{
        error_t e;
        i64 life;

        if (!anon)
                return -E_IN_PARAM;

        life = port_ops_life_get(anon);
        if (life == PORT_OPS_LIFE_CLOSED)
                return REND_SUCCESS;

        if (life != PORT_OPS_LIFE_OPEN) {
                pr_error("[named_port] ensure_closed bad life=%d\n", (int)life);
                return -E_RENDEZVOS;
        }

        e = close_port(anon);
        if (e != REND_SUCCESS) {
                pr_error("[named_port] close_port failed e=%d life=%d\n",
                         (int)e,
                         (int)port_ops_life_get(anon));
                return e;
        }
        return REND_SUCCESS;
}

error_t free_named_message_port_ref(ref_count_t* ref_count_ptr)
{
        Named_Message_Port_t* named_port;
        Message_Port_t* anon;
        struct allocator* cpu_kallocator;
        error_t e;

        if (!ref_count_ptr)
                return -E_IN_PARAM;
        named_port =
                container_of(ref_count_ptr, Named_Message_Port_t, refcount);
        anon = (Message_Port_t*)named_port;

        if (named_port_is_in_table(named_port)) {
                pr_error(
                        "[named_port] free_ref still in table name=%s life=%ld\n",
                        named_port->name,
                        port_ops_life_get(anon));
                return -E_RENDEZVOS;
        }
        e = named_port_ensure_closed(anon);
        if (e != REND_SUCCESS)
                return e;

        if (named_port->append_hooks && named_port->append_hooks->fini)
                named_port->append_hooks->fini(named_port);

        list_del_init(&anon->owner_link);

        cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator)
                cpu_kallocator->m_free(cpu_kallocator, named_port);
        return REND_SUCCESS;
}

bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Named_Message_Port_t* port)
{
        struct spin_lock_t* cpu_mcs_node;
        Named_Message_Port_t* found;
        bool is_live;

        if (!table || !name || !port)
                return false;

        cpu_mcs_node = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, cpu_mcs_node);
        found = (Named_Message_Port_t*)name_index_search(
                &table->by_name, name, NULL);
        is_live = (found && found == port && named_port_is_in_table(found));
        unlock_mcs(&table->by_name.lock, cpu_mcs_node);
        return is_live;
}

struct Port_Table* port_table_create(void)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        struct Port_Table* table = (struct Port_Table*)cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(struct Port_Table));

        if (table)
                port_table_init(table);
        return table;
}

void port_table_init(struct Port_Table* table)
{
        if (!table)
                return;
        name_index_init(&table->by_name,
                        percpu(kallocator),
                        PORT_NAME_LEN_MAX,
                        table,
                        named_port_index_get_name,
                        named_port_index_hold,
                        named_port_index_drop,
                        named_port_index_on_register,
                        named_port_index_on_unregister);
}

void delete_port_table_structure(struct Port_Table* table)
{
        if (!table)
                return;
        name_index_fini(&table->by_name);
        table->by_name.alloc->m_free(table->by_name.alloc, table);
}

error_t register_named_port(struct Port_Table* table,
                            Named_Message_Port_t* port)
{
        struct spin_lock_t* cpu_mcs_node;
        Named_Message_Port_t* existing;
        u64 reg_row_idx;

        if (!table || !port || !port->name[0])
                return -E_IN_PARAM;

        cpu_mcs_node = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, cpu_mcs_node);

        if (!port_is_open((Message_Port_t*)port)) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_REND_PORT_CLOSED;
        }
        if (named_port_is_in_table(port) && port->table != table) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_RENDEZVOS;
        }

        existing = (Named_Message_Port_t*)name_index_search(
                &table->by_name, port->name, NULL);
        if (existing) {
                if (existing == port) {
                        unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                        return REND_SUCCESS;
                }
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_RENDEZVOS;
        }
        error_t e = named_port_ops_allow(port, PORT_OPS_REGISTER, NULL);

        if (e != REND_SUCCESS) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return e;
        }

        reg_row_idx = 0;
        if (name_index_register(&table->by_name, (void*)port, &reg_row_idx)
            != REND_SUCCESS) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_RENDEZVOS;
        }
        if (!ref_get_not_zero(&port->refcount)) {
                name_index_register_abort(
                        &table->by_name, reg_row_idx, (void*)port);
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_RENDEZVOS;
        }
        unlock_mcs(&table->by_name.lock, cpu_mcs_node);
        return REND_SUCCESS;
}

error_t unregister_named_port(struct Port_Table* table, const char* name)
{
        struct spin_lock_t* cpu_mcs_node;
        u64 row_idx;
        Named_Message_Port_t* port;
        error_t e;

        if (!table || !name)
                return -E_IN_PARAM;

        /*
         * Hold an extra ref across the unlocked close: a concurrent
         * unregister could otherwise leave + drop the table pin while we still
         * have only a bare search pointer.
         */
        cpu_mcs_node = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, cpu_mcs_node);
        row_idx = 0;
        port = (Named_Message_Port_t*)name_index_search(
                &table->by_name, name, &row_idx);
        if (!port || !named_port_is_in_table(port)) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return REND_SUCCESS;
        }
        if (!ref_get_not_zero(&port->refcount)) {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                return -E_RENDEZVOS;
        }
        unlock_mcs(&table->by_name.lock, cpu_mcs_node);

        e = named_port_ensure_closed((Message_Port_t*)port);
        if (e != REND_SUCCESS) {
                pr_error("[named_port] unregister close failed e=%d name=%s\n",
                         (int)e,
                         name);
                named_port_index_drop(port);
                return e;
        }

        lock_mcs(&table->by_name.lock, cpu_mcs_node);
        row_idx = 0;
        /* Re-search: a concurrent unregister may have left already. */
        if ((Named_Message_Port_t*)name_index_search(
                    &table->by_name, name, &row_idx)
                    == port
            && named_port_is_in_table(port)) {
                name_index_unregister(
                        &table->by_name, (void*)port, row_idx, name);
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
                /* Table pin*/
                named_port_index_drop(port);
        } else {
                unlock_mcs(&table->by_name.lock, cpu_mcs_node);
        }
        named_port_index_drop(port); /* temporary hold */
        return REND_SUCCESS;
}

static Named_Message_Port_t*
port_lookup_finish(Named_Message_Port_t* named_port, const char* lookup_name)
{
        if (!named_port)
                return NULL;

        if (named_port_ops_allow(named_port, PORT_OPS_LOOKUP, lookup_name)
            != REND_SUCCESS) {
                named_port_index_drop(named_port);
                return NULL;
        }
        return named_port;
}

Named_Message_Port_t* port_table_lookup(struct Port_Table* table,
                                        const char* name)
{
        Named_Message_Port_t* named_port;

        if (!table || !name)
                return NULL;
        named_port = (Named_Message_Port_t*)name_index_lookup(
                &table->by_name, name, NULL);
        return port_lookup_finish(named_port, name);
}

Named_Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                                   const char* name,
                                                   name_index_token_t* tok_out)
{
        Named_Message_Port_t* named_port;

        if (!table || !name)
                return NULL;
        named_port = (Named_Message_Port_t*)name_index_lookup(
                &table->by_name, name, tok_out);
        return port_lookup_finish(named_port, name);
}

Named_Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                               const name_index_token_t* tok,
                                               const char* name)
{
        Named_Message_Port_t* named_port;

        if (!table || !name)
                return NULL;
        if (!tok) {
                named_port = (Named_Message_Port_t*)name_index_lookup(
                        &table->by_name, name, NULL);
                return port_lookup_finish(named_port, name);
        }
        named_port = (Named_Message_Port_t*)name_index_resolve(
                &table->by_name, tok, name);
        return port_lookup_finish(named_port, name);
}

struct Port_Table* global_port_table;

error_t global_port_init(void)
{
        global_port_table = port_table_create();
        if (!global_port_table) {
                pr_error("[ PORT ] Failed to create global port table\n");
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;
}

error_t named_port_send_msg(Named_Message_Port_t* port)
{
        error_t e = named_port_ops_allow(port, PORT_OPS_SEND, NULL);
        if (e != REND_SUCCESS)
                return e;
        return send_msg((Message_Port_t*)port);
}

error_t named_port_try_send_msg(Named_Message_Port_t* port)
{
        error_t e = named_port_ops_allow(port, PORT_OPS_SEND, NULL);
        if (e != REND_SUCCESS)
                return e;
        return ipc_try_send_msg((Message_Port_t*)port);
}

error_t named_port_recv_msg(Named_Message_Port_t* port)
{
        error_t e = named_port_ops_allow(port, PORT_OPS_RECV, NULL);
        if (e != REND_SUCCESS)
                return e;
        return recv_msg((Message_Port_t*)port);
}

error_t named_port_try_recv_msg(Named_Message_Port_t* port)
{
        error_t e = named_port_ops_allow(port, PORT_OPS_RECV, NULL);
        if (e != REND_SUCCESS)
                return e;
        return ipc_try_recv_msg((Message_Port_t*)port);
}
