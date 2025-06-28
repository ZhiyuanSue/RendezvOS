#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
#include <common/string.h>
#include "sys_ctrl_def.h"
#define NR_AARCH64_CALLEE_SAVED_REGS 12

/*
in aarch64 ,a0-a7 is used to translate the int parameter
and v0-v7 is used to translate the float parameter
*/
#define NR_ABI_PARAMETER_INT_REG   8
#define NR_ABI_PARAMETER_FLOAT_REG 8

enum aarch64_callee_saved_regs {
        aarch64_task_ctx_x19 = 0,
        aarch64_task_ctx_x20 = 1,
        aarch64_task_ctx_x21 = 2,
        aarch64_task_ctx_x22 = 3,
        aarch64_task_ctx_x23 = 4,
        aarch64_task_ctx_x24 = 5,
        aarch64_task_ctx_x25 = 6,
        aarch64_task_ctx_x26 = 7,
        aarch64_task_ctx_x27 = 8,
        aarch64_task_ctx_x28 = 9,
        aarch64_task_ctx_fp = 10,
        aarch64_task_ctx_lr = 11,
};
typedef struct {
        u64 sp_el1; /*we only need to consider the el1 in task context*/
        u64 spsr_el1;
        /*x19-x30*/
        u64 regs[NR_AARCH64_CALLEE_SAVED_REGS];
        u64 tpidr_el0;
} Arch_Task_Context;
typedef struct {
        void* thread_func_ptr;
        u64 int_para[NR_ABI_PARAMETER_INT_REG];
} Thread_Init_Para;
static inline void arch_task_ctx_init(Arch_Task_Context* ctx)
{
        ctx->sp_el1 = ctx->spsr_el1 = ctx->tpidr_el0 = 0;
        memset(&(ctx->regs), 0, sizeof(u64) * NR_AARCH64_CALLEE_SAVED_REGS);
}
static inline void arch_set_new_thread_ctx(Arch_Task_Context* ctx,
                                           void* func_ptr, void* stack_bottom)
{
        ctx->sp_el1 = (u64)stack_bottom;
        ctx->regs[aarch64_task_ctx_lr] = (u64)func_ptr;
        /*TODO:should I add spsr el1???*/
}
extern void context_switch(Arch_Task_Context* old_context,
                           Arch_Task_Context* new_context);
void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context);
void arch_drop_to_user(vaddr user_kstack_bottom, vaddr user_sp, vaddr entry);
#endif