#include <rendezvos/ipc/port.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/spin_lock.h>
#include <common/string.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(struct spin_lock_t, port_table_spin_lock);

static const char* port_get_name(void* v)
{
        Message_Port_t* p = (Message_Port_t*)v;
        return p ? p->name : NULL;
}

static bool port_hold(void* v)
{
        Message_Port_t* p = (Message_Port_t*)v;
        if (!p)
                return false;
        return ref_get_not_zero(&p->refcount);
}

static void port_drop(void* v)
{
        Message_Port_t* p = (Message_Port_t*)v;
        if (!p)
                return;
        ref_put(&p->refcount, free_message_port_ref);
}

static void port_on_register(void* v, void* owner_context)
{
        Message_Port_t* p = (Message_Port_t*)v;
        struct Port_Table* table = (struct Port_Table*)owner_context;
        if (!p)
                return;
        p->registered = true;
        p->table = table;
}

static void port_on_unregister(void* v, void* owner_context)
{
        Message_Port_t* p = (Message_Port_t*)v;
        (void)owner_context;
        if (!p)
                return;
        p->registered = false;
        p->table = NULL;
}

/*
 * Stable 16-bit service id derived from the port name.
 * This is used only for fast validation (kmsg_hdr.module); routing uses the
 * port name string.
 */
static u16 service_id_from_name(const char* name)
{
        if (!name || !name[0])
                return 0;
        u32 h = 2166136261u;
        for (u32 i = 0; i < PORT_NAME_LEN_MAX && name[i] != '\0'; i++) {
                h ^= (u8)name[i];
                h *= 16777619u;
        }
        u16 id = (u16)(h ^ (h >> 16));
        if (id == 0)
                id = 1;
        return id;
}

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
        mp->service_id = service_id_from_name(mp->name);
        mp->registered = false;

        return mp;
}

void delete_message_port_structure(Message_Port_t* port)
{
        if (!port)
                return;
        msq_clean_queue(&port->thread_queue, true, free_ipc_request);
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator && !cpu_kallocator->m_free) {
                pr_error(
                        "[port] delete_message_port_structure: m_free is NULL cpu=%lx port_hi=%lx port_lo=%lx\n",
                        (u32)percpu(cpu_number),
                        (u32)(((u64)(uintptr_t)port >> 32) & 0xffffffff),
                        (u32)((u64)(uintptr_t)port & 0xffffffff));
        }
        if (cpu_kallocator)
                cpu_kallocator->m_free(cpu_kallocator, port);
}

bool port_table_port_is_live(struct Port_Table* table, const char* name,
                             Message_Port_t* port)
{
        if (!table || !name || !port)
                return false;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, my_lock);
        Message_Port_t* p =
                (Message_Port_t*)name_index_search(&table->by_name, name, NULL);
        bool ok = (p == port && port->registered);
        unlock_mcs(&table->by_name.lock, my_lock);
        return ok;
}

static void free_message_port_ref_real(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Message_Port_t* port =
                container_of(ref_count_ptr, Message_Port_t, refcount);

        /*
         * Final free must not mutate the port table index.
         * register/unregister is the only legal path to add/remove entries.
         */
        if (port->registered || port->table) {
                pr_error(
                        "[port] free_ref still registered name=%s reg=%lx table_hi=%lx table_lo=%lx\n",
                        port->name,
                        (u32)port->registered,
                        (u32)(((u64)(uintptr_t)port->table >> 32) & 0xffffffff),
                        (u32)((u64)(uintptr_t)port->table & 0xffffffff));
        }

        delete_message_port_structure(port);
}

error_t free_message_port_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return -E_IN_PARAM;
        free_message_port_ref_real(ref_count_ptr);
        return REND_SUCCESS;
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
                        port_get_name,
                        port_hold,
                        port_drop,
                        port_on_register,
                        port_on_unregister);
}

void delete_port_table_structure(struct Port_Table* table)
{
        if (!table)
                return;
        name_index_fini(&table->by_name);
        table->by_name.alloc->m_free(table->by_name.alloc, table);
}

error_t register_port(struct Port_Table* table, Message_Port_t* port)
{
        if (!table || !port || !port->name[0])
                return -E_IN_PARAM;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, my_lock);
        Message_Port_t* existing = (Message_Port_t*)name_index_search(
                &table->by_name, port->name, NULL);
        if (existing) {
                if (existing == port) {
                        unlock_mcs(&table->by_name.lock, my_lock);
                        return REND_SUCCESS;
                }
                unlock_mcs(&table->by_name.lock, my_lock);
                return -E_RENDEZVOS;
        }

        u64 reg_row_idx = 0;
        if (name_index_register(&table->by_name, (void*)port, &reg_row_idx)
            != REND_SUCCESS) {
                unlock_mcs(&table->by_name.lock, my_lock);
                return -E_RENDEZVOS;
        }
        /* table holds one ref */
        if (!ref_get_not_zero(&port->refcount)) {
                name_index_register_abort(
                        &table->by_name, reg_row_idx, (void*)port);
                unlock_mcs(&table->by_name.lock, my_lock);
                return -E_RENDEZVOS;
        }
        unlock_mcs(&table->by_name.lock, my_lock);
        return REND_SUCCESS;
}

error_t unregister_port(struct Port_Table* table, const char* name)
{
        if (!table || !name)
                return -E_IN_PARAM;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->by_name.lock, my_lock);
        u64 row_idx = 0;
        Message_Port_t* port = (Message_Port_t*)name_index_search(
                &table->by_name, name, &row_idx);
        if (!port || !port->registered) {
                unlock_mcs(&table->by_name.lock, my_lock);
                return REND_SUCCESS;
        }

        name_index_unregister(&table->by_name, (void*)port, row_idx, name);
        unlock_mcs(&table->by_name.lock, my_lock);

        ref_put(&port->refcount, free_message_port_ref);
        return REND_SUCCESS;
}

Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name)
{
        if (!table || !name)
                return NULL;
        return (Message_Port_t*)name_index_lookup(&table->by_name, name, NULL);
}

Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                             const char* name,
                                             name_index_token_t* tok_out)
{
        if (!table || !name)
                return NULL;
        return (Message_Port_t*)name_index_lookup(
                &table->by_name, name, tok_out);
}

Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const name_index_token_t* tok,
                                         const char* name)
{
        if (!table || !name)
                return NULL;
        if (!tok)
                return (Message_Port_t*)name_index_lookup(
                        &table->by_name, name, NULL);
        return (Message_Port_t*)name_index_resolve(&table->by_name, tok, name);
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
