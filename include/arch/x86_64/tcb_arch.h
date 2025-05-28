#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
/* This is based on System V AMD64 ABI*/
typedef struct {
        u64 rsp;
        /*following is the callee saved regs*/
        u64 r15;
        u64 r14;
        u64 r13;
        u64 r12;
        u64 rbp;
        u64 rbx;
} Arch_Task_Context;
static inline void arch_task_ctx_init(Arch_Task_Context* ctx)
{
        ctx->rsp = ctx->r15 = ctx->r14 = ctx->r13 = ctx->r12 = ctx->rbp =
                ctx->rbx = 0;
}
#endif