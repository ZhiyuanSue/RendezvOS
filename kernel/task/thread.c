#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/task/port.h>
#include <rendezvos/sync/spin_lock.h>

/*
we first generate a context that after the return will goto thread entry（this
function) then the stack frame is the only one thread_entry frame then here we
change the return addr, change the parameter then after this return , we will
run the target function
*/
static void thread_entry(void)
{
        // pr_info("go into the thread_entry\n");
        Thread_Base* current_thread = percpu(core_tm)->current_thread;
        /*get the parameter*/
        if (!current_thread->init_parameter) {
                pr_error("[Error] no any target func is set\n");
                return;
        }
        /*run the target thread*/
        run_thread(current_thread->init_parameter);
        /*
                        finish run the target thread and prepare the clean
                        But the following code should be unreachable for user
           thread (but not kernel thread) in run thread func, when it drop to
           the user address, the sp should set to the kernel stack bottom , so
           the trace of the thread_entry and run_thread will be cover
                        */
        pr_info("go back to thread entry and try to clean\n");
        thread_or_flags(current_thread, THREAD_FLAG_EXIT_REQUESTED);
        schedule(percpu(core_tm));
}

static void thread_port_cache_init(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        cache->count = 0;
        cache->lru_clock = 1;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                cache->entries[i].lru_counter = 0;
                cache->entries[i].name_hash = 0;
                cache->entries[i].slot_index = PORT_TABLE_SLOT_INDEX_INVALID;
                cache->entries[i].slot_gen = 0;
        }
}

static void thread_port_cache_clear(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                cache->entries[i].lru_counter = 0;
                cache->entries[i].name_hash = 0;
                cache->entries[i].slot_index = PORT_TABLE_SLOT_INDEX_INVALID;
                cache->entries[i].slot_gen = 0;
        }
        cache->count = 0;
        cache->lru_clock = 1;
}

Thread_Base* new_thread_structure(struct allocator* cpu_kallocator,
                                  size_t append_thread_info_len)
{
        if (!cpu_kallocator)
                return NULL;
        Thread_Base* thread = (Thread_Base*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Thread_Base) + append_thread_info_len));
        if (!thread) {
                goto alloc_thread_error;
        }
        memset((void*)thread, 0, sizeof(Thread_Base) + append_thread_info_len);

        /*first do alloc*/
        thread->init_parameter = new_init_parameter_structure();
        if (!thread->init_parameter) {
                goto alloc_init_param_error;
        }

        Message_t* dummy_recv_msg_node = (Message_t*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Message_t)));
        if (!dummy_recv_msg_node) {
                goto alloc_dummy_recv_msg_error;
        }
        Message_t* dummy_send_msg_node = (Message_t*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Message_t)));
        if (!dummy_send_msg_node) {
                goto alloc_dummy_send_msg_error;
        }

        thread->tid = INVALID_ID;
        arch_task_ctx_init(&(thread->ctx));
        thread_set_status(thread, thread_status_init);
        INIT_LIST_HEAD(&(thread->sched_thread_list));
        INIT_LIST_HEAD(&(thread->thread_list_node));
        thread->belong_tcb = NULL;
        thread->tm = NULL;
        thread->kstack_bottom = 0;
        thread->kstack_num = thread_kstack_page_num;
        thread->name = NULL;
        thread->flags = THREAD_FLAG_NONE;

        /*ipc part*/
        memset(dummy_recv_msg_node, 0, sizeof(Message_t));
        msq_init(&thread->recv_msg_queue,
                 &dummy_recv_msg_node->ms_queue_node,
                 0);

        memset(dummy_send_msg_node, 0, sizeof(Message_t));
        msq_init(&thread->send_msg_queue,
                 &dummy_send_msg_node->ms_queue_node,
                 0);

        thread->send_pending_msg = NULL;
        thread->recv_pending_cnt.counter = 0;
        thread_port_cache_init(&thread->port_cache);
        return thread;

alloc_dummy_send_msg_error:
        cpu_kallocator->m_free(cpu_kallocator, dummy_recv_msg_node);
alloc_dummy_recv_msg_error:
        cpu_kallocator->m_free(cpu_kallocator, thread->init_parameter);
alloc_init_param_error:
        cpu_kallocator->m_free(cpu_kallocator, thread);
alloc_thread_error:
        return NULL;
}
void del_thread_structure(Thread_Base* thread)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!thread || !cpu_kallocator)
                return;
        thread_port_cache_clear(&thread->port_cache);
        del_init_parameter_structure(thread->init_parameter);
        cpu_kallocator->m_free(cpu_kallocator, thread);
}
void free_thread_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return;
        Thread_Base* thread =
                container_of(ref_count_ptr, Thread_Base, refcount);
        del_thread_structure(thread);
}

Thread_Init_Para* new_init_parameter_structure(void)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator)
                return NULL;
        Thread_Init_Para* new_pm = (Thread_Init_Para*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Thread_Init_Para)));
        if (new_pm) {
                new_pm->thread_func_ptr = NULL;
                memset(new_pm->int_para,
                       '\0',
                       (NR_ABI_PARAMETER_INT_REG) * sizeof(u64));
        }
        return new_pm;
}
void del_init_parameter_structure(Thread_Init_Para* pm)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!pm || !cpu_kallocator)
                return;
        cpu_kallocator->m_free(cpu_kallocator, (void*)pm);
}
/*general thread create function*/
Thread_Base* create_thread(void* __func, size_t append_thread_info_len,
                           int nr_parameter, ...)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Thread_Base* thread =
                new_thread_structure(cpu_kallocator, append_thread_info_len);
        if (!thread) {
                goto new_thread_structure_error;
        }
        ref_init(&thread->refcount);
        thread->tid = get_new_id(&tid_manager);
        va_list arg_list;
        va_start(arg_list, nr_parameter);

        size_t stack_bytes = thread_kstack_page_num * PAGE_SIZE;
        void* kstack = cpu_kallocator->m_alloc(cpu_kallocator, stack_bytes);
        if (!kstack) {
                goto get_kstack_error;
        }
        thread->kstack_bottom =
                (vaddr)kstack + thread_kstack_page_num * PAGE_SIZE;
        memset(kstack, '\0', thread_kstack_page_num * PAGE_SIZE);
        arch_set_new_thread_ctx(&(thread->ctx),
                                (void*)(thread_entry),
                                kstack + thread_kstack_page_num * PAGE_SIZE);
        /*
        set the init parameter of the thread
        the parameter must no more then the NR_ABI_PARAMETER_INT_REG
        and must all are integer, otherwise more parameters will be ignore
        */
        for (int i = 0; i < nr_parameter && i < NR_ABI_PARAMETER_INT_REG; i++) {
                /*here we think in rendezvos kernel,we only use the int
                 * parameters*/
                thread->init_parameter->int_para[i] = va_arg(arg_list, u64);
        }
        thread->init_parameter->thread_func_ptr = __func;
        va_end(arg_list);
        return thread;
get_kstack_error:
        del_thread_structure(thread);
new_thread_structure_error:
        return NULL;
}
void delete_thread(Thread_Base* thread)
{
        if (!thread || !thread->belong_tcb)
                return;
        atomic64_store(&thread->status, thread_status_exit);
        /*free the send pending msg*/
        Message_t* pending_msg = (Message_t*)atomic64_exchange(
                (volatile u64*)(&thread->send_pending_msg), (u64)NULL);
        if (pending_msg) {
                ref_put(&pending_msg->ms_queue_node.refcount, free_message_ref);
        }
        /*clean the send msg queue*/
        clean_message_queue(&thread->send_msg_queue, true);
        /*clean the recv msg queue*/
        clean_message_queue(&thread->recv_msg_queue, true);

        error_t e = -E_RENDEZVOS;
        if (thread->kstack_bottom) {
                /*
                 * at some time,
                 * if you using the new_thread_structure and try to use
                 * delete_thread, the kstack bottom is 0,but not an error
                 */
                void* thread_stack_start = (void*)thread->kstack_bottom
                                           - thread->kstack_num * PAGE_SIZE;
                struct allocator* cpu_kallocator = percpu(kallocator);
                if (cpu_kallocator)
                        cpu_kallocator->m_free(cpu_kallocator,
                                               thread_stack_start);
        }
        e = del_thread_from_task(thread);
        if (e) {
                pr_error(
                        "[ Error ] delete thread from task fail, please check\n");
        }
        if (thread->tm) {
                e = del_thread_from_manager(thread);
                if (e) {
                        pr_error(
                                "[ Error ] delete thread from manager fail,please check\n");
                }
        }
        ref_put(&thread->refcount, free_thread_ref);
        return;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread)
{
        if (!task || !thread) {
                return -E_IN_PARAM;
        }
        error_t res = 0;
        res = add_thread_to_task(task, thread);
        if (res)
                return res;
        res = add_thread_to_manager(percpu(core_tm), thread);
        thread_set_status(thread, thread_status_ready);
        return res;
}

/*
 * Per-thread port name cache: bounded name copy + FNV-1a hash + table token.
 * Does not pin Message_Port_t; each hit re-validates under the port table lock.
 */

/* FNV-1a 64-bit over at most PORT_NAME_LEN_MAX-1 chars of `name` (bounded). */
static u64 thread_port_name_hash(const char* name)
{
        const u64 offset = 14695981039346656037ULL;
        const u64 prime = 1099511628211ULL;
        u64 h = offset;

        if (!name)
                return 0;
        for (u32 i = 0; i < PORT_NAME_LEN_MAX && name[i]; i++) {
                h ^= (u64)(u8)name[i];
                h *= prime;
        }
        return h;
}

static bool thread_port_cache_slot_occupied(struct thread_port_cache* c, u32 i)
{
        return c && i < THREAD_MAX_KNOWN_PORTS
               && c->entries[i].slot_index != PORT_TABLE_SLOT_INDEX_INVALID;
}

static void thread_port_cache_evict(struct thread_port_cache* c, u32 idx)
{
        if (!thread_port_cache_slot_occupied(c, idx))
                return;
        c->entries[idx].lru_counter = 0;
        c->entries[idx].slot_gen = 0;
        c->entries[idx].slot_index = PORT_TABLE_SLOT_INDEX_INVALID;
        c->entries[idx].name_hash = 0;
        if (c->count > 0)
                c->count--;
}

static void thread_port_cache_fill_entry(struct thread_port_cache* c, u32 idx,
                                         const char* name,
                                         const port_table_slot_token_t* tok)
{
        /* O(1) LRU update: uses wrap-safe u16 age via lru_clock. */
        c->lru_clock++;
        c->entries[idx].lru_counter = (u16)c->lru_clock;
        c->entries[idx].name_hash = thread_port_name_hash(name);
        if (tok) {
                c->entries[idx].slot_index = tok->slot_index;
                c->entries[idx].slot_gen = tok->slot_gen;
        } else {
                c->entries[idx].slot_index = PORT_TABLE_SLOT_INDEX_INVALID;
                c->entries[idx].slot_gen = 0;
        }
}

static void thread_port_cache_bump_lru(struct thread_port_cache* c,
                                       u32 found_idx)
{
        if (!thread_port_cache_slot_occupied(c, found_idx))
                return;
        c->lru_clock++;
        c->entries[found_idx].lru_counter = (u16)c->lru_clock;
}

static Message_Port_t* thread_port_cache_lookup(Thread_Base* thread,
                                                const char* name)
{
        struct thread_port_cache* c = &thread->port_cache;
        u64 target_hash = thread_port_name_hash(name);
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                if (!thread_port_cache_slot_occupied(c, i))
                        continue;
                if (c->entries[i].name_hash != target_hash)
                        continue;

                /* If hash collides, we may need to try multiple candidates. */
                port_table_slot_token_t tok;
                tok.slot_index = c->entries[i].slot_index;
                tok.slot_gen = c->entries[i].slot_gen;
                Message_Port_t* port =
                        port_table_resolve_token(global_port_table, &tok, name);
                if (!port) {
                        /* Stale token (gen mismatch / unregistered) */
                        thread_port_cache_evict(c, i);
                        continue;
                }

                thread_port_cache_bump_lru(c, i);
                return port;
        }
        return NULL;
}

static void thread_port_cache_record(Thread_Base* thread, const char* name,
                                     const port_table_slot_token_t* tok)
{
        struct thread_port_cache* c = &thread->port_cache;
        u64 target_hash = thread_port_name_hash(name);

        /* Reuse exact cached token (slot_index+slot_gen), not just hash. */
        if (tok) {
                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!thread_port_cache_slot_occupied(c, i))
                                continue;
                        if (c->entries[i].slot_index != tok->slot_index
                            || c->entries[i].slot_gen != tok->slot_gen)
                                continue;
                        thread_port_cache_bump_lru(c, i);
                        c->entries[i].name_hash = target_hash;
                        c->entries[i].slot_index = tok->slot_index;
                        c->entries[i].slot_gen = tok->slot_gen;
                        return;
                }
        }

        if (c->count < THREAD_MAX_KNOWN_PORTS) {
                u32 empty_idx = THREAD_MAX_KNOWN_PORTS;
                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!thread_port_cache_slot_occupied(c, i)) {
                                empty_idx = i;
                                break;
                        }
                }
                if (empty_idx < THREAD_MAX_KNOWN_PORTS) {
                        thread_port_cache_fill_entry(c, empty_idx, name, tok);
                        c->count++;
                        return;
                }
                /* count desync or table full — fall through to LRU replace */
        }

        /* Wrap-safe eviction: choose entry with maximum modular age (u16). */
        u16 cur = (u16)c->lru_clock;
        u32 lru_idx = 0;
        u16 best_age = 0;
        bool found = false;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                if (!thread_port_cache_slot_occupied(c, i))
                        continue;
                u16 age = (u16)(cur - c->entries[i].lru_counter);
                if (!found || age > best_age) {
                        best_age = age;
                        lru_idx = i;
                        found = true;
                }
        }
        if (!found)
                return;
        thread_port_cache_fill_entry(c, lru_idx, name, tok);
}

/*You have to use ref put after finish use of this function*/
Message_Port_t* thread_lookup_port(const char* name)
{
        Thread_Base* self = get_cpu_current_thread();
        if (!self || !name || !global_port_table)
                return NULL;

        Message_Port_t* port = thread_port_cache_lookup(self, name);
        if (port) {
                return port;
        }

        port_table_slot_token_t cold_tok;
        port_table_slot_token_invalidate(&cold_tok);
        port = port_table_lookup_with_token(global_port_table, name, &cold_tok);
        if (!port) {
                return NULL;
        }

        thread_port_cache_record(self, name, &cold_tok);

        return port;
}
