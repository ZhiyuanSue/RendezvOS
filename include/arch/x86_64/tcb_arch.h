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
        ctx->rsp = 0;
        ctx->rbp = ctx->rbx = 0;
        ctx->r15 = ctx->r14 = 0;
        ctx->r13 = ctx->r12 = 0;
}
static inline void arch_set_new_thread_ctx(Arch_Task_Context* ctx,
                                           void* idle_thread_ptr,
                                           void* stack_bottom)
{
        /*here the stack_bottom - 16 is rflags, and the stack_bottom - 8 is
         * return address*/
        *((u64*)(stack_bottom - sizeof(u64))) = (vaddr)idle_thread_ptr;
        ctx->rsp = (vaddr)stack_bottom - sizeof(u64) * 2;
}
#endif