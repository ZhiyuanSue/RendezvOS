#ifndef _RENDEZVOS_TCB_H_
#define _RENDEZVOS_TCB_H_

#include <common/types.h>
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

enum tcb_status_base {
        tcb_status_running,
        tcb_status_active_ready,
        tcb_status_suspend_ready,
        tcb_status_active_blocked,
        tcb_status_suspend_blocked,
};
#define TCB_COMMON  \
        u64 pid;    \
        u64 tid;    \
        u64 status; \
        Arch_Context ctx;

extern void context_switch(Arch_Context* old_context,
                           Arch_Context* new_context);
/* as the base class of tcb */
struct tcb_base {
        TCB_COMMON
};

error_t init_proc();
error_t create_idle_thread();
#endif