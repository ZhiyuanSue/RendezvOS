#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
/* This is based on System V AMD64 ABI*/
/*
under the System V AMD64 ABI
integer parameters can be translate by 6 registers
which are rdi,rsi,rdx,rcx,r8,r9
float parameters can be translate by xmm0-xmm7
in rendezvos, we do not use xmm regs in kernel,but we list the number
*/
#define NR_ABI_PARAMETER_INT_REG   6
#define NR_ABI_PARAMETER_FLOAT_REG 8

typedef struct {
        u64 rsp;
        /*following is the callee saved regs*/
        u64 r15;
        u64 r14;
        u64 r13;
        u64 r12;
        u64 rbp;
        u64 rbx;
        u64 stack_bottom;
} Arch_Task_Context;
typedef struct {
        void* thread_func_ptr;
        u64 int_para[NR_ABI_PARAMETER_INT_REG];
} Thread_Init_Para;
static inline void arch_task_ctx_init(Arch_Task_Context* ctx)
{
        ctx->rsp = ctx->stack_bottom = 0;
        ctx->rbp = ctx->rbx = 0;
        ctx->r15 = ctx->r14 = 0;
        ctx->r13 = ctx->r12 = 0;
}
static inline void arch_set_new_thread_ctx(Arch_Task_Context* ctx,
                                           void* func_ptr, void* stack_bottom)
{
        /*here the stack_bottom - 16 is rflags, and the stack_bottom - 8 is
         * return address*/
        *((u64*)(stack_bottom - sizeof(u64))) = (vaddr)func_ptr;
        ctx->rsp = (vaddr)stack_bottom - sizeof(u64) * 2;
        ctx->stack_bottom = (vaddr)stack_bottom;
}
extern void context_switch(Arch_Task_Context* old_context,
                           Arch_Task_Context* new_context);
void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context);
void arch_drop_to_user(vaddr user_sp, vaddr entry);
#endif