#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
#include <common/string.h>
#define NR_AARCH64_CALLEE_SAVED_REGS 12
typedef struct {
        u64 sp_el1; /*we only need to consider the el1 in task context*/
        u64 spsr_el1;
        /*x19-x30*/
        u64 regs[NR_AARCH64_CALLEE_SAVED_REGS];
} Arch_Task_Context;
static inline void arch_task_ctx_init(Arch_Task_Context* ctx)
{
        ctx->sp_el1 = ctx->spsr_el1 = 0;
        memset(&(ctx->regs), 0, sizeof(u64) * NR_AARCH64_CALLEE_SAVED_REGS);
}
#endif