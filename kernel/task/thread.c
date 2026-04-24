#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/ipc/port.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/system/panic.h>

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
                cache->entries[i].row_index = NAME_INDEX_ROW_INDEX_INVALID;
                cache->entries[i].row_gen = 0;
        }
}

static void thread_port_cache_clear(struct thread_port_cache* cache)
{
        if (!cache)
                return;
        for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                cache->entries[i].lru_counter = 0;
                cache->entries[i].name_hash = 0;
                cache->entries[i].row_index = NAME_INDEX_ROW_INDEX_INVALID;
                cache->entries[i].row_gen = 0;
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

/*
 * Drain heap resources owned by the thread.
 *
 * Call only from del_thread_structure when the last ref on thread->refcount
 * drops (free_thread_ref), or from synchronous teardown paths that do not go
 * through delete_thread (e.g. create_thread failure).
 *
 * Do not call from delete_thread before ref_put: delete_thread already runs
 * del_thread_from_* then ref_put, and del_thread_structure runs again on last
 * ref. A second drain after head/tail were zeroed makes msq_dequeue spin
 * forever (!head_node branch + continue). MSQ dummy is ref_put by msq_dequeue
 * on the empty-queue path; msq_clean_queue only zeroes head/tail—never
 * ref_put(dummy) twice.
 *
 * Teardown contract:
 * 1) delete_thread: detach from task + Task_Manager sched ring (del_thread_*),
 *    then ref_put — thread struct may survive if IPC etc. still holds a ref.
 * 2) Last ref -> free_thread_ref -> del_thread_structure -> here + init + free.
 */
static void thread_release_owned_resources(Thread_Base* thread)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!thread || !cpu_kallocator)
                return;

        Message_t* pending_msg = (Message_t*)atomic64_exchange(
                (volatile u64*)(&thread->send_pending_msg), (u64)NULL);
        if (pending_msg) {
                ref_put(&pending_msg->ms_queue_node.refcount, free_message_ref);
        }
        msq_clean_queue(&thread->send_msg_queue, true, free_message_ref);
        msq_clean_queue(&thread->recv_msg_queue, true, free_message_ref);

        if (thread->name) {
                void* name_buf = (void*)thread->name;
                thread->name = NULL;
                cpu_kallocator->m_free(cpu_kallocator, name_buf);
        }

        if (thread->kstack_bottom) {
                void* thread_stack_start = (void*)thread->kstack_bottom
                                           - thread->kstack_num * PAGE_SIZE;
                cpu_kallocator->m_free(cpu_kallocator, thread_stack_start);
                thread->kstack_bottom = 0;
        }
        atomic64_store((volatile u64*)&thread->port_ptr, (u64)NULL);
        thread_port_cache_clear(&thread->port_cache);
}

void del_thread_structure(Thread_Base* thread)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!thread || !cpu_kallocator)
                return;
        /*
         * Last-chance: ensure not still linked on task or scheduler ring before
         * freeing Thread_Base (refcount path may skip delete_thread).
         */
        (void)del_thread_from_task(thread);
        (void)del_thread_from_manager(thread);
        thread_release_owned_resources(thread);
        del_init_parameter_structure(thread->init_parameter);
        thread->init_parameter = NULL;
        cpu_kallocator->m_free(cpu_kallocator, thread);
}
error_t free_thread_ref(ref_count_t* ref_count_ptr)
{
        if (!ref_count_ptr)
                return -E_IN_PARAM;
        Thread_Base* thread =
                container_of(ref_count_ptr, Thread_Base, refcount);
        del_thread_structure(thread);
        return REND_SUCCESS;
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
                           bool reserve_trap_frame, int nr_parameter, ...)
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
                                (void*)(thread->kstack_bottom),
                                reserve_trap_frame);
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
        /* Heap/queues/kstack: released in del_thread_structure on last ref
         * only. */

        error_t e = -E_RENDEZVOS;
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
 * Per-thread port name cache: FNV-1a hash + cached name_index token (row, gen).
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

static bool thread_port_cache_entry_in_use(struct thread_port_cache* c, u32 i)
{
        return c && i < THREAD_MAX_KNOWN_PORTS
               && c->entries[i].row_index != NAME_INDEX_ROW_INDEX_INVALID;
}

static void thread_port_cache_evict(struct thread_port_cache* c, u32 idx)
{
        if (!thread_port_cache_entry_in_use(c, idx))
                return;
        c->entries[idx].lru_counter = 0;
        c->entries[idx].row_gen = 0;
        c->entries[idx].row_index = NAME_INDEX_ROW_INDEX_INVALID;
        c->entries[idx].name_hash = 0;
        if (c->count > 0)
                c->count--;
}

static void thread_port_cache_fill_entry(struct thread_port_cache* c, u32 idx,
                                         const char* name,
                                         const name_index_token_t* tok)
{
        /* O(1) LRU update: uses wrap-safe u16 age via lru_clock. */
        c->lru_clock++;
        c->entries[idx].lru_counter = (u16)c->lru_clock;
        c->entries[idx].name_hash = thread_port_name_hash(name);
        if (tok) {
                c->entries[idx].row_index = tok->row_index;
                c->entries[idx].row_gen = tok->row_gen;
        } else {
                c->entries[idx].row_index = NAME_INDEX_ROW_INDEX_INVALID;
                c->entries[idx].row_gen = 0;
        }
}

static void thread_port_cache_bump_lru(struct thread_port_cache* c,
                                       u32 found_idx)
{
        if (!thread_port_cache_entry_in_use(c, found_idx))
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
                if (!thread_port_cache_entry_in_use(c, i))
                        continue;
                if (c->entries[i].name_hash != target_hash)
                        continue;

                /* If hash collides, we may need to try multiple candidates. */
                name_index_token_t tok;
                tok.row_index = c->entries[i].row_index;
                tok.row_gen = c->entries[i].row_gen;
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
                                     const name_index_token_t* tok)
{
        struct thread_port_cache* c = &thread->port_cache;
        u64 target_hash = thread_port_name_hash(name);

        /* Reuse exact cached token (row_index+row_gen), not just hash. */
        if (tok) {
                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!thread_port_cache_entry_in_use(c, i))
                                continue;
                        if (c->entries[i].row_index != tok->row_index
                            || c->entries[i].row_gen != tok->row_gen)
                                continue;
                        thread_port_cache_bump_lru(c, i);
                        c->entries[i].name_hash = target_hash;
                        c->entries[i].row_index = tok->row_index;
                        c->entries[i].row_gen = tok->row_gen;
                        return;
                }
        }

        if (c->count < THREAD_MAX_KNOWN_PORTS) {
                u32 empty_idx = THREAD_MAX_KNOWN_PORTS;
                for (u32 i = 0; i < THREAD_MAX_KNOWN_PORTS; i++) {
                        if (!thread_port_cache_entry_in_use(c, i)) {
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
                if (!thread_port_cache_entry_in_use(c, i))
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

        name_index_token_t cold_tok;
        name_index_token_invalidate(&cold_tok);
        port = port_table_lookup_with_token(global_port_table, name, &cold_tok);
        if (!port) {
                return NULL;
        }

        thread_port_cache_record(self, name, &cold_tok);

        return port;
}

/*
 * Child thread bootstrap: thread_entry → run_thread → here.
 *
 * int_para[0] = syscall return value for child.
 *
 * Parent's trap_frame was already copied to child's kstack save slot in
 * copy_thread (by direct struct assignment). This function only sets the
 * syscall return value in that copied frame and returns to user mode. It does
 * NOT access parent's kstack.
 */
void run_copied_thread(u64 syscall_return_value)
{
        Thread_Base* current_thread = get_cpu_current_thread();

        if (!current_thread || !current_thread->kstack_bottom) {
                pr_error("[run_copied_thread] no current thread or kstack\n");
                goto run_thread_end;
        }
        arch_return_to_user(
                current_thread->kstack_bottom, NULL, syscall_return_value);
run_thread_end:
        pr_info("go back to thread entry and try to clean\n");
        thread_or_flags(current_thread, THREAD_FLAG_EXIT_REQUESTED);
        schedule(percpu(core_tm));
}

Thread_Base* copy_thread(Thread_Base* src_thread, Tcb_Base* target_task,
                         u64 custom_return_value, size_t append_thread_info_len)
{
        struct trap_frame* src_trap_frame;
        struct trap_frame* dst_trap_frame;
        struct allocator* cpu_allocator = percpu(kallocator);

        if (!src_thread || !target_task || !cpu_allocator) {
                return NULL;
        }

        if (!(src_thread->flags & THREAD_FLAG_USER)) {
                pr_error("[copy_thread] parent is not a user thread\n");
                return NULL;
        }

        /*
         * Same boot path as create_thread (thread_entry + init_parameter), then
         * arch_ctx_merge_from_src copies user/callee-visible context from the
         * source thread while preserving the new thread's kernel bootstrap
         * fields (e.g. x86 rsp/stack_bottom, aarch64 sp_el1/LR).
         */
        Thread_Base* dst_thread = create_thread((void*)run_copied_thread,
                                                append_thread_info_len,
                                                true,
                                                1,
                                                custom_return_value);
        if (!dst_thread) {
                pr_error("[copy_thread] create_thread failed\n");
                return NULL;
        }

        src_trap_frame = ((struct trap_frame*)(src_thread->kstack_bottom)) - 1;
        dst_trap_frame = ((struct trap_frame*)(dst_thread->kstack_bottom)) - 1;
        *dst_trap_frame = *src_trap_frame;

        arch_ctx_merge_from_src(&dst_thread->ctx, &src_thread->ctx);

        dst_thread->belong_tcb = target_task;
        dst_thread->flags = src_thread->flags;

        if (src_thread->name) {
                size_t name_len = strlen(src_thread->name) + 1;
                char* name_copy =
                        cpu_allocator->m_alloc(cpu_allocator, name_len);

                if (name_copy) {
                        memcpy(name_copy, src_thread->name, name_len);
                        dst_thread->name = name_copy;
                } else {
                        pr_error("[copy_thread] name alloc failed\n");
                        dst_thread->name = NULL;
                }
        } else {
                dst_thread->name = NULL;
        }

        if (append_thread_info_len > 0) {
                memcpy(dst_thread->append_thread_info,
                       src_thread->append_thread_info,
                       append_thread_info_len);
        }

        thread_set_status(dst_thread, thread_status_ready);

        pr_debug("[copy_thread] Created dst_thread thread tid=%d\n",
                 dst_thread->tid);

        return dst_thread;
}
