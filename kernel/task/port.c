#include <rendezvos/task/port.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/spin_lock.h>
#include <common/string.h>
#include <modules/log/log.h>
#include "port_table_slots.h"

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

        return mp;
}

void delete_message_port_structure(Message_Port_t* port)
{
        if (!port)
                return;
        /*TODO: clean the queue*/
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator && !cpu_kallocator->m_free) {
                pr_error(
                        "[port] delete_message_port_structure: m_free is NULL cpu=%x port_hi=%x port_lo=%x\n",
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
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* p = port_slots_search(table, name, NULL);
        bool ok = (p == port && port->registered);
        unlock_mcs(&table->lock, my_lock);
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
                        "[port] free_ref still registered name=%s reg=%x table_hi=%x table_lo=%x\n",
                        port->name,
                        (u32)port->registered,
                        (u32)(((u64)(uintptr_t)port->table >> 32) & 0xffffffff),
                        (u32)((u64)(uintptr_t)port->table & 0xffffffff));
        }

        delete_message_port_structure(port);
}

void free_message_port_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        free_message_port_ref_real(ref_count_ptr);
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
        table->lock = NULL;
        table->alloc = percpu(kallocator);
        port_slots_table_init(table);
}

void delete_port_table_structure(struct Port_Table* table)
{
        if (!table)
                return;
        port_slots_table_fini(table);
        table->alloc->m_free(table->alloc, table);
}

error_t register_port(struct Port_Table* table, Message_Port_t* port)
{
        if (!table || !port || !port->name[0])
                return -E_IN_PARAM;

        struct spin_lock_t* my_lock = &percpu(port_table_spin_lock);
        lock_mcs(&table->lock, my_lock);
        Message_Port_t* existing = port_slots_search(table, port->name, NULL);
        if (existing) {
                if (existing == port) {
                        unlock_mcs(&table->lock, my_lock);
                        return REND_SUCCESS;
                }
                unlock_mcs(&table->lock, my_lock);
                return -E_RENDEZVOS;
        }

        u64 reg_slot_idx = 0;
        if (port_slots_register(table, port, &reg_slot_idx) != REND_SUCCESS) {
                unlock_mcs(&table->lock, my_lock);
                return -E_RENDEZVOS;
        }
        /* table holds one ref */
        if (!ref_get_not_zero(&port->refcount)) {
                port_slots_register_abort(table, reg_slot_idx, port);
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
        u64 slot_idx = 0;
        Message_Port_t* port = port_slots_search(table, name, &slot_idx);
        if (!port || !port->registered) {
                unlock_mcs(&table->lock, my_lock);
                return REND_SUCCESS;
        }

        port_slots_unregister(table, port, slot_idx, name);
        unlock_mcs(&table->lock, my_lock);

        ref_put(&port->refcount, free_message_port_ref);
        return REND_SUCCESS;
}

Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name)
{
        if (!table || !name)
                return NULL;
        return port_slots_lookup(table, name, NULL);
}

Message_Port_t* port_table_lookup_with_token(struct Port_Table* table,
                                             const char* name,
                                             port_table_slot_token_t* tok_out)
{
        if (!table || !name)
                return NULL;
        return port_slots_lookup(table, name, tok_out);
}

Message_Port_t* port_table_resolve_token(struct Port_Table* table,
                                         const port_table_slot_token_t* tok,
                                         const char* name)
{
        if (!table || !name)
                return NULL;
        if (!tok)
                return port_slots_lookup(table, name, NULL);
        return port_slots_resolve(table, tok, name);
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
