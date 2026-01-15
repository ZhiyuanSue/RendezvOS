#ifndef _RENDEZVOS_TCB_H_
#define _RENDEZVOS_TCB_H_

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/kmalloc.h>
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

/*thread and process all have ipc part*/
#define TASK_IPC_COMMON                  \
        ms_queue_node_t port_queue_node; \
        ms_queue_t recv_msg_queue;       \
        ms_queue_t send_msg_queue;

typedef struct {
        TASK_IPC_COMMON;
        void* belong_thread;
        void* belong_process;
} Task_Ipc_Base;

Task_Ipc_Base* new_task_ipc_base_structure(struct allocator* cpu_allocator);
void delete_task_ipc_base_structure(Task_Ipc_Base* task_ipc_info);

enum thread_status_base {
        thread_status_init,
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
        i64 pid;                            \
        Task_Manager* tm;                   \
        i64 thread_number;                  \
        struct list_entry thread_head_node; \
        VSpace* vs;                         \
        Task_Ipc_Base* ipc_info;            \
        TASK_SCHE_COMMON
/* as the base class of tcb */
typedef struct {
        TCB_COMMON
} Tcb_Base;

/* thread */
extern u64 thread_kstack_page_num;
#define THERAD_SCHE_COMMON                           \
        struct {                                     \
                struct list_entry sched_thread_list; \
        };
#define THREAD_COMMON                                   \
        char* name;                                     \
        i64 tid;                                        \
        u64 flags;                                      \
        Tcb_Base* belong_tcb;                           \
        Task_Manager* tm;                               \
        u64 status;                                     \
        struct list_entry thread_list_node;             \
        u64 kstack_bottom; /*for stack,it's high addr*/ \
        u64 kstack_num;                                 \
        Arch_Task_Context ctx;                          \
        Thread_Init_Para* init_parameter;               \
        Task_Ipc_Base* ipc_info;                        \
        THERAD_SCHE_COMMON

#define THREAD_FLAG_NONE               0
#define THREAD_FLAG_KERNEL_USER_OFFSET (0)
#define THREAD_FLAG_USER               (0x1ull)
/*let the default is kernel thread*/

typedef struct {
        THREAD_COMMON
} Thread_Base;

extern Thread_Base* init_thread_ptr;
extern Thread_Base* idle_thread_ptr;
struct task_manager {
        TASK_MANAGER_SCHE_COMMON
        Tcb_Base* current_task;
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
Tcb_Base* new_task_structure(struct allocator* cpu_allocator);
Task_Manager* new_task_manager();
void del_thread_structure(Thread_Base* thread);
Thread_Base* new_thread_structure(struct allocator* cpu_allocator);

Thread_Init_Para* new_init_parameter_structure();
void del_init_parameter_structure(Thread_Init_Para* pm);

error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread);
error_t del_thread_from_task(Tcb_Base* task, Thread_Base* thread);
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task);
error_t del_task_from_manager(Tcb_Base* task);
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread);
error_t del_thread_from_manager(Thread_Base* thread);

error_t create_init_thread(Tcb_Base* root_task);
error_t create_idle_thread(Tcb_Base* root_task);

Thread_Base* create_thread(void* __func, int nr_parameter, ...);
void delete_thread(Thread_Base* thread);
void delete_task(Tcb_Base* tcb);

static inline Thread_Base* get_cpu_current_thread()
{
        return percpu(core_tm)->current_thread;
}
static inline Tcb_Base* get_cpu_current_task()
{
        return percpu(core_tm)->current_task;
}

static inline void thread_set_flags(u64 flags, Thread_Base* thread)
{
        thread->flags = flags;
}
static inline u64 thread_get_status(Thread_Base* thread)
{
        return thread->status;
}
static inline void thread_set_status(u64 status, Thread_Base* thread)
{
        thread->status = status;
}
static inline void thread_set_name(char* name, Thread_Base* thread)
{
        thread->name = name;
}
error_t thread_join(Tcb_Base* task, Thread_Base* thread);
void list_all_threads(Task_Manager* tm);

#endif