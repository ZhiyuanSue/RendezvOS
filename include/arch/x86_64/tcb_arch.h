#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
#include <common/string.h>
#include <arch/x86_64/trap/trap.h>
#include <rendezvos/smp/percpu.h>
/*
 * RFLAGS image for arch_x86_sched_switch_pair (POPF then RET in
 * context_switch).
 * - Bit 1: Intel SDM documents this reserved bit as 1 in several RFLAGS images.
 * - Bit 9 (IF): enable interrupts after the new thread starts running.
 */
#define X86_RFLAGS_FIXED_RESERVED1 (1ULL << 1)
#define X86_RFLAGS_IF              (1ULL << 9)
#define X86_RFLAGS_ENTRY           (X86_RFLAGS_FIXED_RESERVED1 | X86_RFLAGS_IF)

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
        u64 user_gs;
        u64 user_fs;
        u64 user_rsp;
} Arch_Task_Context;

/*
 * Per-CPU scratch for saving the live user RSP on syscall/trap entry.
 *
 * On x86_64, the entry path stores the current user-mode RSP into this
 * per-CPU slot before switching stacks. This is the authoritative user RSP
 * while executing in kernel on that CPU.
 *
 * Defined in `core/arch/x86_64/task/arch_thread.c`.
 */
extern vaddr user_rsp_scratch;

/* Declared in arch-specific task/arch_thread.c (context merge & syscall-trap
 * return). */
void arch_ctx_merge_from_src(Arch_Task_Context* dst_ctx,
                             const Arch_Task_Context* src_ctx);
/*
 * Refresh context fields from the live CPU state while running in kernel
 * during a user->kernel transition (e.g. syscall handling).
 *
 * Motivation: some user-mode visible state is captured by the arch entry path
 * into per-CPU scratch/MSRs, and `Arch_Task_Context` may only be synchronized
 * on context switch. Fork/copy performed inside syscall context must use the
 * live values to avoid returning to user mode with stale state.
 */
void arch_ctx_refresh(Arch_Task_Context* ctx);
void arch_return_to_user(u64 kstack_bottom,
                         const struct trap_frame* template_tf, u64 syscall_ret);

/* Zeroed syscall-shaped frame for first ELF entry; rcx = user RIP (sysret). */
static inline void arch_empty_drop_trap_frame(struct trap_frame* tf,
                                              vaddr entry_addr)
{
        memset(tf, 0, sizeof(*tf));
        tf->rcx = (u64)entry_addr;
}

typedef struct {
        void* thread_func_ptr;
        u64 int_para[NR_ABI_PARAMETER_INT_REG];
} Thread_Init_Para;
static inline void arch_task_ctx_init(Arch_Task_Context* ctx)
{
        ctx->rsp = ctx->stack_bottom = ctx->user_rsp = 0;
        ctx->rbp = ctx->rbx = 0;
        ctx->r15 = ctx->r14 = 0;
        ctx->r13 = ctx->r12 = 0;
        ctx->user_gs = ctx->user_fs = 0;
}
static inline void arch_set_new_thread_ctx(Arch_Task_Context* ctx,
                                           void* func_ptr, void* kstack_bottom,
                                           bool reserve_trap_frame)
{
        vaddr bottom = (vaddr)kstack_bottom;
        vaddr sp = bottom;

        if (reserve_trap_frame) {
                sp = (vaddr)(((struct trap_frame*)bottom) - 1);
        }

        /*here the stack_bottom - 16 is rflags, and the stack_bottom - 8 is
         * return address*/
        *((u64*)(sp - sizeof(u64))) = (vaddr)func_ptr;
        sp -= 2 * sizeof(u64);
        *((u64*)sp) = X86_RFLAGS_ENTRY;

        ctx->rsp = sp;
        ctx->stack_bottom = bottom;
}
static inline vaddr arch_get_thread_user_sp(Arch_Task_Context* ctx)
{
        return ctx->user_rsp;
}
static inline void arch_set_thread_user_sp(Arch_Task_Context* ctx,
                                           vaddr user_sp)
{
        ctx->user_rsp = user_sp;
};
extern void context_switch(Arch_Task_Context* old_context,
                           Arch_Task_Context* new_context);
void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context);
void arch_drop_to_user(struct trap_frame* tf);
#endif