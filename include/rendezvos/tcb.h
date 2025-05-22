#ifndef _RENDEZVOS_TCB_H_
#define _RENDEZVOS_TCB_H_

#include <common/types.h>

enum tcb_status_base {
        tcb_status_running,
        tcb_status_active_ready,
        tcb_status_suspend_ready,
        tcb_status_active_blocked,
        tcb_status_suspend_blocked,
};
#define TCB_COMMON \
        u64 pid;   \
        u64 tid;   \
        u64 status;

/* as the base class of tcb */
struct tcb_base {
        TCB_COMMON
};

error_t init_proc();
error_t create_idle_thread();
#endif