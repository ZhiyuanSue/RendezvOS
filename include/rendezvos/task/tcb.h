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
#include "message.h"
#include "port.h"

enum thread_status_base {
        thread_status_error = -1,
        thread_status_init = 0,
        thread_status_running,
        thread_status_ready,
        thread_status_zombie,
        thread_status_block_on_send,
        thread_status_block_on_receive,
        thread_status_cancel_ipc,
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
        VS_Common* vs;                      \
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
        /* (index,gen) cache of port table slot token (narrow types). */
        u16 slot_gen;
        u32 slot_index;
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
        Tcb_Base* current_task;
        Tcb_Base* root_task;
        Thread_Base* current_thread;
        Thread_Base* (*scheduler)(Task_Manager* tm);
};
void schedule(Task_Manager* tm);
extern void run_thread(Thread_Init_Para* para);

/* scheduler */
/* rr scheduler */
Thread_Base* round_robin_schedule(Task_Manager* tm);
void choose_schedule(Task_Manager* tm);

/*return the root task, here we design the root task have only one thread --
 * idle thread*/
Task_Manager* init_proc();
/* general task and thread new function */
Tcb_Base* new_task_structure(struct allocator* cpu_allocator,
                             size_t append_tcb_info_len);
Task_Manager* new_task_manager();
void del_task_manager_structure(Task_Manager* tm);
void del_thread_structure(Thread_Base* thread);
Thread_Base* new_thread_structure(struct allocator* cpu_allocator,
                                  size_t append_thread_info_len);
error_t free_thread_ref(ref_count_t* ref_count_ptr);

Thread_Init_Para* new_init_parameter_structure();
void del_init_parameter_structure(Thread_Init_Para* pm);

error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread);
error_t del_thread_from_task(Thread_Base* thread);
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task);
error_t del_task_from_manager(Tcb_Base* task);
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread);
error_t del_thread_from_manager(Thread_Base* thread);

Thread_Base* create_thread(void* __func, size_t append_thread_info_len,
                           int nr_parameter, ...);
void delete_thread(Thread_Base* thread);
error_t delete_task(Tcb_Base* tcb);

Message_Port_t* thread_lookup_port(const char* name);

static inline Thread_Base* get_cpu_current_thread()
{
        if (!percpu(core_tm))
                return NULL;
        return percpu(core_tm)->current_thread;
}
static inline Tcb_Base* get_cpu_current_task()
{
        if (!percpu(core_tm))
                return NULL;
        return percpu(core_tm)->current_task;
}

static inline void thread_set_flags(Thread_Base* thread, u64 flags)
{
        if (!thread)
                return;
        thread->flags = flags;
}
/** OR bits into thread->flags; preserves existing flags (unlike thread_set_flags). */
static inline void thread_or_flags(Thread_Base* thread, u64 bits)
{
        if (!thread)
                return;
        thread->flags |= bits;
}
static inline u64 thread_get_status(Thread_Base* thread)
{
        if (!thread)
                return thread_status_error;
        return atomic64_load((volatile u64*)(&thread->status));
}
static inline u64 thread_set_status(Thread_Base* thread, u64 status)
{
        if (!thread)
                return thread_status_error;
        return atomic64_exchange((volatile u64*)(&thread->status), status);
}
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
static inline void thread_set_name(char* name, Thread_Base* thread)
{
        if (!name || !thread)
                return;
        thread->name = name;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread);
void list_all_threads(Task_Manager* tm);

#endif
