#include <rendezvos/task/port.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <common/string.h>
#include <common/stddef.h>
#include <modules/log/log.h>

Message_Port_t* create_message_port()
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Message_Port_t* mp =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(Message_Port_t));

        if (mp) {
                Ipc_Request_t* dummy_requeust_node =
                        (Ipc_Request_t*)(cpu_kallocator->m_alloc(
                                cpu_kallocator, sizeof(Ipc_Request_t)));
                /* Dummy must be zeroed so free_thread_ref (via
                 * del_thread_structure) does not dereference garbage
                 * init_parameter when the dummy is dequeued and freed by
                 * msq_dequeue_check_head. */
                if (dummy_requeust_node) {
                        memset(dummy_requeust_node, 0, sizeof(Ipc_Request_t));
                        msq_init(&mp->thread_queue,
                                 &dummy_requeust_node->ms_queue_node,
                                 IPC_PORT_APPEND_BITS);
                }
        } else {
                return NULL;
        }
        return mp;
}
void delete_message_port(Message_Port_t* port)
{
        if (!port)
                return;
        /*TODO: clean the queue*/
        percpu(kallocator)->m_free(percpu(kallocator), port);
}

static struct port_registry global_port_registry;
static bool global_registry_initialized;

static char* port_registry_strdup(const char* name)
{
        size_t len = strlen(name);
        if (len >= PORT_REGISTRY_NAME_MAX)
                return NULL;
        struct allocator* a = percpu(kallocator);
        char* p = (char*)a->m_alloc(a, len + 1);
        if (!p)
                return NULL;
        memcpy(p, name, len + 1);
        return p;
}

static void free_port_registry_entry(ref_count_t* refcount)
{
        if (!refcount)
                return;
        struct port_registry_entry* entry =
                container_of(refcount, struct port_registry_entry, refcount);
        struct port_registry* registry = entry->registry;
        lock_cas(&registry->lock);
        RB_Remove(&entry->rb_node, &registry->root);
        unlock_cas(&registry->lock);
        entry->rb_node.left_child = entry->rb_node.right_child = NULL;
        entry->rb_node.black_height = 0;
        entry->rb_node.rb_parent_color = 0;
        if (entry->name) {
                percpu(kallocator)->m_free(percpu(kallocator), entry->name);
                entry->name = NULL;
        }
        percpu(kallocator)->m_free(percpu(kallocator), entry);
}

static struct port_registry_entry* port_registry_search(
        struct port_registry* registry, const char* name)
{
        struct rb_node* node = registry->root.rb_root;
        while (node) {
                struct port_registry_entry* entry =
                        container_of(node, struct port_registry_entry, rb_node);
                int cmp = strcmp(name, entry->name);
                if (cmp < 0)
                        node = node->left_child;
                else if (cmp > 0)
                        node = node->right_child;
                else
                        return entry;
        }
        return NULL;
}

static void port_registry_init(struct port_registry* registry)
{
        if (!registry)
                return;
        registry->root.rb_root = NULL;
        lock_init_cas(&registry->lock);
        registry->version_counter = 0;
}

static error_t port_registry_register(struct port_registry* registry,
                                      const char* name,
                                      Message_Port_t* port)
{
        if (!registry || !name || !port)
                return -E_IN_PARAM;
        size_t len = strlen(name);
        if (len == 0 || len >= PORT_REGISTRY_NAME_MAX)
                return -E_IN_PARAM;

        lock_cas(&registry->lock);
        struct port_registry_entry* existing =
                port_registry_search(registry, name);
        if (existing) {
                existing->port = port;
                existing->registered = true;
                existing->version++;
                unlock_cas(&registry->lock);
                return REND_SUCCESS;
        }

        struct port_registry_entry* entry = (struct port_registry_entry*)percpu(
                kallocator)
                ->m_alloc(percpu(kallocator), sizeof(struct port_registry_entry));
        if (!entry) {
                unlock_cas(&registry->lock);
                return -E_RENDEZVOS;
        }
        entry->name = port_registry_strdup(name);
        if (!entry->name) {
                percpu(kallocator)->m_free(percpu(kallocator), entry);
                unlock_cas(&registry->lock);
                return -E_RENDEZVOS;
        }
        entry->port = port;
        entry->version = registry->version_counter++;
        entry->registered = true;
        entry->registry = registry;
        ref_init(&entry->refcount); /* registry holds one ref until unregister */

        entry->rb_node.left_child = entry->rb_node.right_child = NULL;
        entry->rb_node.black_height = 0;
        entry->rb_node.rb_parent_color = RB_RED;

        struct rb_node** new_link = &registry->root.rb_root;
        struct rb_node* parent = NULL;
        while (*new_link) {
                parent = *new_link;
                struct port_registry_entry* pentry =
                        container_of(parent, struct port_registry_entry,
                                     rb_node);
                int cmp = strcmp(name, pentry->name);
                if (cmp < 0)
                        new_link = &parent->left_child;
                else
                        new_link = &parent->right_child;
        }
        RB_Link_Node(&entry->rb_node, parent, new_link);
        RB_SolveDoubleRed(&entry->rb_node, &registry->root);
        unlock_cas(&registry->lock);
        return REND_SUCCESS;
}

static error_t port_registry_unregister(struct port_registry* registry,
                                        const char* name)
{
        if (!registry || !name)
                return -E_IN_PARAM;
        lock_cas(&registry->lock);
        struct port_registry_entry* entry =
                port_registry_search(registry, name);
        if (!entry) {
                unlock_cas(&registry->lock);
                return -E_RENDEZVOS;
        }
        entry->port = NULL;
        entry->registered = false;
        entry->version++;
        unlock_cas(&registry->lock);
        ref_put(&entry->refcount, free_port_registry_entry);
        return REND_SUCCESS;
}

static struct port_registry_entry* port_registry_lookup_entry(
        struct port_registry* registry, const char* name)
{
        if (!registry || !name)
                return NULL;
        lock_cas(&registry->lock);
        struct port_registry_entry* entry =
                port_registry_search(registry, name);
        if (!entry || !entry->registered || !entry->port) {
                unlock_cas(&registry->lock);
                return NULL;
        }
        if (!ref_get_not_zero(&entry->refcount)) {
                unlock_cas(&registry->lock);
                return NULL;
        }
        unlock_cas(&registry->lock);
        return entry;
}

static void port_registry_put_entry(struct port_registry* registry,
                                    struct port_registry_entry* entry)
{
        if (!registry || !entry)
                return;
        ref_put(&entry->refcount, free_port_registry_entry);
}

void port_discovery_init(void)
{
        port_registry_init(&global_port_registry);
        global_registry_initialized = true;
}

void thread_port_cache_init(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        cache->count = 0;
        lock_init_cas(&cache->lock);
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                cache->entries[i].name[0] = '\0';
                cache->entries[i].entry = NULL;
                cache->entries[i].version = 0;
                cache->entries[i].lru_counter = 0;
        }
}

void thread_port_cache_clear(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        lock_cas(&cache->lock);
        for (u32 i = 0; i < cache->count; i++) {
                struct port_registry_entry* e = cache->entries[i].entry;
                if (e)
                        port_registry_put_entry(&global_port_registry, e);
                cache->entries[i].entry = NULL;
                cache->entries[i].name[0] = '\0';
                cache->entries[i].lru_counter = 0;
        }
        cache->count = 0;
        unlock_cas(&cache->lock);
}

static struct port_registry_entry* thread_port_cache_lookup(
        Thread_Base* thread, const char* name)
{
        struct thread_port_cache* c = &thread->port_cache;
        struct port_registry_entry* e = NULL;
        u16 cached_version = 0;
        u32 found_idx = 0;
        lock_cas(&c->lock);
        for (u32 i = 0; i < c->count; i++) {
                if (strcmp(c->entries[i].name, name) != 0)
                        continue;
                e = c->entries[i].entry;
                cached_version = c->entries[i].version;
                found_idx = i;
                break;
        }
        
        /* LRU更新：新访问的设为0，其他都+1 */
        if (e) {
                for (u32 i = 0; i < c->count; i++) {
                        if (i == found_idx) {
                                c->entries[i].lru_counter = 0;  /* 最近使用 */
                        } else {
                                c->entries[i].lru_counter++;     /* 其他+1 */
                        }
                }
        }
        unlock_cas(&c->lock);
        
        if (!e)
                return NULL;
        if (e->registered && e->port != NULL && e->version == cached_version)
                return e;
        return NULL;
}

static void thread_port_cache_remove(Thread_Base* thread, const char* name)
{
        struct thread_port_cache* c = &thread->port_cache;
        lock_cas(&c->lock);
        for (u32 i = 0; i < c->count; i++) {
                if (strcmp(c->entries[i].name, name) != 0)
                        continue;
                struct port_registry_entry* e = c->entries[i].entry;
                if (e)
                        port_registry_put_entry(&global_port_registry, e);
                c->count--;
                for (u32 j = i; j < c->count; j++) {
                        c->entries[j] = c->entries[j + 1];
                }
                c->entries[c->count].name[0] = '\0';
                c->entries[c->count].entry = NULL;
                c->entries[c->count].lru_counter = 0;
                unlock_cas(&c->lock);
                return;
        }
        unlock_cas(&c->lock);
}

static error_t thread_port_cache_add(Thread_Base* thread, const char* name,
                                      struct port_registry_entry* entry)
{
        if (!entry || !name)
                return -E_IN_PARAM;
        size_t len = strlen(name);
        if (len >= PORT_CACHE_NAME_LEN)
                return -E_IN_PARAM;
        struct thread_port_cache* c = &thread->port_cache;
        lock_cas(&c->lock);
        
        /* 如果缓存未满，直接添加 */
        if (c->count < THREAD_MAX_KNOWN_PORTS) {
                /* 新添加的设为0，其他+1 */
                for (u32 i = 0; i < c->count; i++) {
                        c->entries[i].lru_counter++;
                }
                memcpy(c->entries[c->count].name, name, len + 1);
                c->entries[c->count].entry = entry;
                c->entries[c->count].version = entry->version;
                c->entries[c->count].lru_counter = 0;  /* 最近使用 */
                c->count++;
                unlock_cas(&c->lock);
                return REND_SUCCESS;
        }
        
        /* 缓存已满，使用LRU淘汰：找到计数最大的条目（最久未使用） */
        u32 lru_idx = 0;
        u16 max_counter = c->entries[0].lru_counter;
        for (u32 i = 1; i < c->count; i++) {
                if (c->entries[i].lru_counter > max_counter) {
                        max_counter = c->entries[i].lru_counter;
                        lru_idx = i;
                }
        }
        
        /* 淘汰LRU条目 */
        struct port_registry_entry* evicted = c->entries[lru_idx].entry;
        if (evicted)
                port_registry_put_entry(&global_port_registry, evicted);
        
        /* 将新条目放在LRU位置，设为0，其他+1 */
        for (u32 i = 0; i < c->count; i++) {
                if (i != lru_idx) {
                        c->entries[i].lru_counter++;
                }
        }
        memcpy(c->entries[lru_idx].name, name, len + 1);
        c->entries[lru_idx].entry = entry;
        c->entries[lru_idx].version = entry->version;
        c->entries[lru_idx].lru_counter = 0;  /* 最近使用 */
        
        unlock_cas(&c->lock);
        return REND_SUCCESS;
}

static char* port_discovery_strdup(const char* name)
{
        size_t len = strlen(name);
        struct allocator* a = percpu(kallocator);
        char* p = (char*)a->m_alloc(a, len + 1);
        if (!p)
                return NULL;
        memcpy(p, name, len + 1);
        return p;
}

error_t thread_register_port(Thread_Base* thread, const char* name)
{
        if (!thread || !name)
                return -E_IN_PARAM;
        if (!global_registry_initialized)
                return -E_RENDEZVOS;

        Message_Port_t* port = thread->exposed_port;
        if (!port) {
                port = create_message_port();
                if (!port)
                        return -E_RENDEZVOS;
                thread->exposed_port = port;
        }

        error_t ret =
                port_registry_register(&global_port_registry, name, port);
        if (ret != REND_SUCCESS)
                return ret;

        if (thread->exposed_port_name)
                percpu(kallocator)->m_free(percpu(kallocator),
                                           thread->exposed_port_name);
        thread->exposed_port_name = port_discovery_strdup(name);
        if (!thread->exposed_port_name) {
                port_registry_unregister(&global_port_registry, name);
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;
}

error_t thread_unregister_port(Thread_Base* thread)
{
        if (!thread)
                return -E_IN_PARAM;
        if (!thread->exposed_port_name)
                return -E_IN_PARAM;

        error_t ret = port_registry_unregister(&global_port_registry,
                                               thread->exposed_port_name);
        percpu(kallocator)->m_free(percpu(kallocator),
                                  thread->exposed_port_name);
        thread->exposed_port_name = NULL;
        return ret;
}

Message_Port_t* thread_lookup_port(const char* name)
{
        Thread_Base* self = get_cpu_current_thread();
        if (!self || !name || !global_registry_initialized)
                return NULL;

        struct port_registry_entry* entry = thread_port_cache_lookup(self, name);
        if (entry)
                return entry->port;

        thread_port_cache_remove(self, name);

        entry = port_registry_lookup_entry(&global_port_registry, name);
        if (!entry)
                return NULL;

        if (thread_port_cache_add(self, name, entry) != REND_SUCCESS)
                port_registry_put_entry(&global_port_registry, entry);
        return entry->port;
}