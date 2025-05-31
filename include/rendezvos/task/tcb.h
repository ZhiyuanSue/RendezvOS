#ifndef _RENDEZVOS_TCB_H_
#define _RENDEZVOS_TCB_H_

#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <rendezvos/mm/mm.h>
#include <rendezvos/mm/spmalloc.h>
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

enum tcb_status_base {
        tcb_status_init,
        tcb_status_running,
        tcb_status_active_ready,
        tcb_status_suspend_ready,
        tcb_status_active_blocked,
        tcb_status_suspend_blocked,
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

/* thread */
#define THERAD_SCHE_COMMON                           \
        struct {                                     \
                struct list_entry sched_thread_list; \
        };
#define THREAD_COMMON                       \
        i64 tid;                            \
        i64 belong_pid;                     \
        Task_Manager* tm;                   \
        u64 status;                         \
        struct list_entry thread_list_node; \
        Arch_Task_Context ctx;              \
        THERAD_SCHE_COMMON

typedef struct {
        THREAD_COMMON
} Thread_Base;

/* task */
#define TASK_SCHE_COMMON                           \
        struct {                                   \
                struct list_entry sched_task_list; \
        };
#define TCB_COMMON                          \
        i64 pid;                            \
        Task_Manager* tm;                   \
        struct list_entry thread_head_node; \
        struct vspace* vs;                  \
        TASK_SCHE_COMMON
/* as the base class of tcb */
typedef struct {
        TCB_COMMON
} Tcb_Base;

extern Thread_Base* current_thread;
struct task_manager {
        TASK_MANAGER_SCHE_COMMON
        Thread_Base* (*schedule)(Task_Manager* tm);
};
extern void context_switch(Arch_Task_Context* old_context,
                           Arch_Task_Context* new_context);

/* scheduler */
/* rr scheduler */
Thread_Base* round_robin_schedule(Task_Manager* tm);
void choose_schedule(Task_Manager* tm);

/*return the root task, here we design the root task have only one thread --
 * idle thread*/
Task_Manager* init_proc();
/* general task and thread new function */
Tcb_Base* new_task();
Task_Manager* new_task_manager();
Thread_Base* new_thread();
error_t add_thread_to_task(Tcb_Base* task, Thread_Base* thread);
error_t del_thread_from_task(Tcb_Base* task, Thread_Base* thread);
error_t add_task_to_manager(Task_Manager* core_tm, Tcb_Base* task);
error_t add_thread_to_manager(Task_Manager* core_tm, Thread_Base* thread);

error_t create_idle_thread(Tcb_Base* root_task);

#endif