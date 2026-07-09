#ifndef _RENDEZVOS_TCB_H_
#define _RENDEZVOS_TCB_H_

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/sync/cas_lock.h>
#ifdef _AARCH64_
#include <arch/aarch64/tcb_arch.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/tcb_arch.h>
#elif defined _RISCV64_
#include <arch/riscv64/tcb_arch.h>
#elif defined _X86_64_
#include <arch/x86_64/tcb_arch.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/tcb_arch.h>
#endif

#include "id.h"
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>

enum thread_status_base {
        thread_status_error = -1,
        thread_status_init = 0,
        thread_status_running,
        thread_status_ready,
        thread_status_zombie,
        thread_status_block_on_send,
        thread_status_block_on_receive,
        thread_status_suspend,
        thread_status_exit,
};

/*
we define a task manager ,
which is used to manage all the tasks and all the threads
it is a percpu structure and have a percpu schedule algorithm
*/
#define TASK_MANAGER_SCHE_COMMON                     \
        struct {                                     \
                cas_lock_t sched_lock;               \
                struct list_entry sched_task_list;   \
                struct list_entry sched_thread_list; \
        };
typedef struct task_manager Task_Manager;
extern Task_Manager* core_tm;

struct Tcb_Base;
struct Thread_Base;

/**
 * @brief Optional hook before freeing @c append_tcb_info tail (ABI-neutral).
 * Called from delete_task() immediately before the TCB allocation is returned
 * to the allocator. Upper layers release heap objects referenced from append.
 */
typedef void (*task_append_fini_t)(struct Tcb_Base* tcb);

/**
 * @brief Optional hook before freeing @c append_thread_info tail.
 * Called from del_thread_structure() immediately before the Thread_Base
 * allocation is returned to the allocator.
 */
typedef void (*thread_append_fini_t)(struct Thread_Base* thread);

/* task */
#define TASK_SCHE_COMMON                           \
        struct {                                   \
                struct list_entry sched_task_list; \
        };
#define TCB_COMMON                          \
        pid_t pid;                          \
        Task_Manager* tm;                   \
        cas_lock_t thread_list_lock;        \
        i64 thread_number;                  \
        struct list_entry thread_head_node; \
        VSpace* vs;                         \
        size_t append_tcb_info_len;         \
        task_append_fini_t append_fini;     \
        TASK_SCHE_COMMON
/* as the base class of tcb */
typedef struct {
        TCB_COMMON
        u64 append_tcb_info[];
} Tcb_Base;

/* Thread port cache */
#define THREAD_MAX_KNOWN_PORTS 16

struct thread_port_cache_entry {
        /* Wrap-safe LRU age uses u16 counter. */
        u16 lru_counter;
        /* (index,gen) cache of name_index_token (narrow types). */
        u16 row_gen;
        u32 row_index;
        u64 name_hash;
};

struct thread_port_cache {
        struct thread_port_cache_entry entries[THREAD_MAX_KNOWN_PORTS];
        u64 count;
        /* Monotonic counter for O(1) LRU updates (no scanning bump). */
        u64 lru_clock;
};

/* thread */
extern u64 thread_kstack_page_num;
#define THERAD_SCHE_COMMON                           \
        struct {                                     \
                struct list_entry sched_thread_list; \
        };
#define THREAD_COMMON                                               \
        char* name;                                                 \
        tid_t tid;                                                  \
        u64 flags;                                                  \
        Tcb_Base* belong_tcb;                                       \
        Task_Manager* tm;                                           \
        u64 status;                                                 \
        struct list_entry thread_list_node;                         \
        u64 kstack_bottom; /*for stack,it's high addr*/             \
        u64 kstack_num;                                             \
        Arch_Task_Context ctx;                                      \
        Thread_Init_Para* init_parameter;                           \
        ref_count_t refcount;                                       \
        ms_queue_t recv_msg_queue;                                  \
        ms_queue_t send_msg_queue;                                  \
        volatile Message_t* send_pending_msg; /* expect Message_t*/ \
        atomic64_t recv_pending_cnt; /*how much msg arrive*/        \
        volatile void* port_ptr; /*expect Message_Port_t*/          \
        struct thread_port_cache port_cache;                        \
        size_t append_thread_info_len;                              \
        thread_append_fini_t append_fini;                           \
        THERAD_SCHE_COMMON

#define THREAD_FLAG_NONE               0
#define THREAD_FLAG_KERNEL_USER_OFFSET (0)
#define THREAD_FLAG_USER               (0x1ull)
/*
 * Set once when a thread requests exit (syscall or kernel thread_entry path).
 * Exit intent is expressed only via this flag (not a dedicated thread status)
 * so IPC paths that set thread_status_block_on_send / block_on_receive cannot
 * erase it. Owner CPU moves running -> zombie when switching away; clean
 * server waits on THREAD_FLAG_EXIT_REQUESTED until status is zombie.
 */
#define THREAD_FLAG_EXIT_REQUESTED (0x2ull)
/*let the default is kernel thread*/

struct Thread_Base {
        THREAD_COMMON
        u64 append_thread_info[];
};
typedef struct Thread_Base Thread_Base;

extern Thread_Base* init_thread_ptr;
extern Thread_Base* idle_thread_ptr;
extern volatile bool is_print_sche_info;
struct task_manager {
        TASK_MANAGER_SCHE_COMMON
        cpu_id_t owner_cpu;
        Tcb_Base* root_task;
        Thread_Base* current_thread;
        Thread_Base* (*scheduler)(Task_Manager* tm);
};
/**
 * @brief Run the scheduler on @p tm: pick next ready thread, switch context,
 * and update running/ready status.
 * @param tm Per-CPU task manager (may be NULL; no-op).
 */
void schedule(Task_Manager* tm);

/**
 * @brief Arch entry: invoke @p para->thread_func_ptr with int_para[] (see
 * create_thread).
 * @param para Thread init parameters for the current thread.
 */
extern void run_thread(Thread_Init_Para* para);

/**
 * @brief Round-robin: next thread on sched_thread_list with status ready.
 * @param tm Task manager whose current_thread anchors the ring walk.
 * @return Next ready thread, or NULL if @p tm or current_thread is NULL.
 */
Thread_Base* round_robin_schedule(Task_Manager* tm);

/**
 * @brief Install default scheduler (round_robin_schedule) on @p tm.
 * @param tm Task manager (no-op if NULL).
 */
void choose_schedule(Task_Manager* tm);

/**
 * @brief Bootstrap per-CPU task manager, root task, init and idle threads;
 * switches from idle to init thread context.
 * @return Per-CPU core_tm on success, or NULL on failure.
 */
Task_Manager* init_proc();

/**
 * @brief Allocate and zero-initialize a task control block (plus optional
 * tail).
 * @param cpu_allocator Allocator for the TCB allocation.
 * @param append_tcb_info_len Extra bytes after TCB_COMMON for extensions.
 * @param append_fini Optional pre-free hook (NULL if @p append_tcb_info_len is
 *        0 or append has no heap-backed extensions).
 * @return New TCB, or NULL if @p cpu_allocator is NULL or allocation fails.
 */
Tcb_Base* new_task_structure(struct allocator* cpu_allocator,
                             size_t append_tcb_info_len,
                             task_append_fini_t append_fini);

/**
 * @brief Allocate a per-CPU task manager and set default scheduler.
 * @return New task manager, or NULL if allocation fails.
 */
Task_Manager* new_task_manager();

/**
 * @brief Free task manager structure only (caller must detach resources first).
 * @param tm Task manager to free (no-op if NULL).
 */
void del_task_manager_structure(Task_Manager* tm);

/**
 * @brief Free thread structure after last refcount drop (detaches from
 * task/manager rings, drains IPC queues and kstack).
 * @param thread Thread to destroy (no-op if NULL).
 */
void del_thread_structure(Thread_Base* thread);

/**
 * @brief Allocate and initialize a thread control block (plus optional tail).
 * @param cpu_allocator Allocator for thread, init params, and IPC dummy nodes.
 * @param append_thread_info_len Extra bytes after THREAD_COMMON for extensions.
 * @param append_fini Optional pre-free hook (NULL if no append teardown).
 * @return New thread, or NULL on allocation failure.
 */
Thread_Base* new_thread_structure(struct allocator* cpu_allocator,
                                  size_t append_thread_info_len,
                                  thread_append_fini_t append_fini);

/**
 * @brief Refcount destructor: calls del_thread_structure for the owning thread.
 * @param ref_count_ptr Pointer to thread->refcount.
 * @return REND_SUCCESS, or -E_IN_PARAM if @p ref_count_ptr is NULL.
 */
error_t free_thread_ref(ref_count_t* ref_count_ptr);

/**
 * @brief Allocate empty thread init-parameter block.
 * @return New parameters, or NULL if per-CPU allocator is unavailable.
 */
Thread_Init_Para* new_init_parameter_structure();

/**
 * @brief Free thread init-parameter block.
 * @param pm Parameters to free (no-op if NULL).
 */
void del_init_parameter_structure(Thread_Init_Para* pm);

/**
 * @brief Link @p thread into @p task's thread list and set belong_tcb.
 * @param task Owning task.
 * @param thread Thread to attach.
 * @return REND_SUCCESS; -E_IN_PARAM if either pointer is NULL; -E_RENDEZVOS if
 *         thread already belongs to another task.
 */
error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread);

/**
 * @brief Unlink @p thread from its task's thread list (idempotent if detached).
 * @param thread Thread to detach.
 * @return REND_SUCCESS, or -E_IN_PARAM if @p thread is NULL.
 */
error_t del_thread_from_task(Thread_Base* thread);

/**
 * @brief Link @p task into @p core_tm sched_task_list.
 * @param core_tm Task manager.
 * @param task Task to schedule.
 * @return REND_SUCCESS; -E_IN_PARAM if pointers invalid; -E_RENDEZVOS if task
 *         already has a manager.
 */
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task);

/**
 * @brief Unlink @p task from its task manager sched_task_list.
 * @param task Task to detach.
 * @return REND_SUCCESS; -E_IN_PARAM if @p task is NULL; -E_RENDEZVOS if task
 * has no manager.
 */
error_t del_task_from_manager(Tcb_Base* task);

/**
 * @brief Link @p thread into @p core_tm sched_thread_list.
 * @param core_tm Task manager.
 * @param thread Thread to schedule.
 * @return REND_SUCCESS; -E_RENDEZVOS if thread already has a manager. If either
 *         pointer is NULL, returns REND_SUCCESS without linking.
 */
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread);

/**
 * @brief Unlink @p thread from its task manager sched_thread_list (idempotent).
 * @param thread Thread to detach.
 * @return REND_SUCCESS, or -E_IN_PARAM if @p thread is NULL.
 */
error_t del_thread_from_manager(Thread_Base* thread);

/**
 * @brief Create a kernel thread that enters via thread_entry then run_thread.
 * @param __func Target function pointer stored in init_parameter.
 * @param append_thread_info_len Extra bytes after THREAD_COMMON.
 * @param append_fini Optional pre-free hook for append_thread_info (NULL ok).
 * @param reserve_trap_frame Whether arch context reserves a trap frame slot.
 * @param nr_parameter Number of u64 varargs (capped by
 * NR_ABI_PARAMETER_INT_REG).
 * @return New thread with refcount initialized, or NULL on failure.
 */
Thread_Base* create_thread(void* __func, size_t append_thread_info_len,
                           thread_append_fini_t append_fini,
                           bool reserve_trap_frame, int nr_parameter, ...);

/**
 * @brief Mark thread exiting, detach from task and manager, drop refcount.
 * @param thread Thread to delete (no-op if NULL or not attached to a task).
 */
void delete_thread(Thread_Base* thread);

/**
 * @brief Remove task from manager and free TCB when it has no threads.
 * @param tcb Task to delete.
 * @return REND_SUCCESS; -E_IN_PARAM if @p tcb is NULL; -E_RENDEZVOS if threads
 *         remain or vspace teardown fails.
 */
error_t delete_task(Tcb_Base* tcb);

/**
 * @brief Look up a message port by name for the current thread (per-thread LRU
 * cache; caller must ref_put the returned port).
 * @param name Port name string.
 * @return Port with refcount bumped, or NULL if not found or bad context.
 */
Message_Port_t* thread_lookup_port(const char* name);

/**
 * @brief Current thread on this CPU's task manager.
 * @return current_thread, or NULL if core_tm is not set.
 */
static inline Thread_Base* get_cpu_current_thread()
{
        if (!percpu(core_tm))
                return NULL;
        return percpu(core_tm)->current_thread;
}

/**
 * @brief Set current thread on this CPU's task manager.
 * No-op if core_tm is not set.
 */
static inline void set_cpu_current_thread(Thread_Base* thread)
{
        Task_Manager* tm = percpu(core_tm);

        if (!tm)
                return;
        tm->current_thread = thread;
}

/**
 * @brief Task owning the current thread, or root_task if belong_tcb is unset.
 * @return Owning TCB, or NULL if no task manager or current thread.
 */
static inline Tcb_Base* get_cpu_current_task(void)
{
        Task_Manager* tm = percpu(core_tm);
        if (!tm || !tm->current_thread)
                return NULL;
        if (tm->current_thread->belong_tcb)
                return tm->current_thread->belong_tcb;
        return tm->root_task;
}

/**
 * @brief Replace thread->flags with @p flags.
 * @param thread Thread (no-op if NULL).
 * @param flags New flag word.
 */
static inline void thread_set_flags(Thread_Base* thread, u64 flags)
{
        if (!thread)
                return;
        thread->flags = flags;
}

/**
 * @brief OR @p bits into thread->flags (preserves existing flags).
 * @param thread Thread (no-op if NULL).
 * @param bits Flag bits to set.
 */
static inline void thread_or_flags(Thread_Base* thread, u64 bits)
{
        if (!thread)
                return;
        thread->flags |= bits;
}

/**
 * @brief Atomic load of thread scheduler/IPC status.
 * @param thread Thread (if NULL, returns thread_status_error).
 * @return Status value.
 */
static inline u64 thread_get_status(Thread_Base* thread)
{
        if (!thread)
                return thread_status_error;
        return atomic64_load((volatile u64*)(&thread->status));
}

/**
 * @brief Atomic exchange of thread status.
 * @param thread Thread (if NULL, returns thread_status_error).
 * @param status New status.
 * @return Previous status.
 */
static inline u64 thread_set_status(Thread_Base* thread, u64 status)
{
        if (!thread)
                return thread_status_error;
        return atomic64_exchange((volatile u64*)(&thread->status), status);
}

/**
 * @brief Atomic CAS of thread status.
 * @param thread Thread (if NULL, returns false).
 * @param expect_status Expected current status.
 * @param target_status Status to store on success.
 * @return true if swap succeeded.
 */
static inline bool thread_set_status_with_expect(Thread_Base* thread,
                                                 u64 expect_status,
                                                 u64 target_status)
{
        if (!thread)
                return false;
        return atomic64_cas((volatile u64*)(&thread->status),
                            expect_status,
                            target_status)
               == expect_status;
}

/**
 * @brief Set thread display name (does not copy string).
 * @param name Name buffer owned by caller.
 * @param thread Thread (no-op if either pointer is NULL).
 */
static inline void thread_set_name(char* name, Thread_Base* thread)
{
        if (!name || !thread)
                return;
        thread->name = name;
}

/**
 * @brief Attach @p thread to @p task and per-CPU manager; mark ready for
 * scheduling. Not a blocking wait-for-exit: does not reap the thread. Adds to
 * task list, adds to percpu(core_tm), and sets thread_status_ready.
 * @param task Task that will own the thread.
 * @param thread Detached or new thread to register.
 * @return REND_SUCCESS; -E_IN_PARAM if pointers are NULL; error from
 *         add_thread_to_task or add_thread_to_manager on failure.
 */
error_t thread_join(Tcb_Base* task, Thread_Base* thread);

/**
 * @brief User-thread bootstrap after copy_thread: return to user with the given
 * value in the trap frame.
 * @param syscall_return_value Value written into the child's user trap frame.
 */
void run_copied_thread(u64 syscall_return_value);

/**
 * @brief Duplicate a user thread into @p target_task (trap frame and arch ctx).
 * @param parent_thread Source user thread (must have THREAD_FLAG_USER).
 * @param target_task Task that will own the child (belong_tcb set; not joined).
 * @param custom_return_value Stored in child init int_para[0] for
 * run_copied_thread.
 * @param append_thread_info_len Bytes to copy from parent append_thread_info.
 * @return New thread in ready status, or NULL on error.
 */
struct Thread_Base* copy_thread(Thread_Base* parent_thread,
                                Tcb_Base* target_task, u64 custom_return_value,
                                size_t append_thread_info_len);

#endif
