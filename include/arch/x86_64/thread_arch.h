#ifndef _RENDEZVOS_THREAD_ARCH_
#define _RENDEZVOS_THREAD_ARCH_

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
} Arch_Thread_Context;

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
void arch_ctx_merge_from_src(Arch_Thread_Context* dst_ctx,
                             const Arch_Thread_Context* src_ctx);
/*
 * Refresh context fields from the live CPU state while running in kernel
 * during a user->kernel transition (e.g. syscall handling).
 *
 * Motivation: some user-mode visible state is captured by the arch entry path
 * into per-CPU scratch/MSRs, and `Arch_Thread_Context` may only be synchronized
 * on context switch. Fork/copy performed inside syscall context must use the
 * live values to avoid returning to user mode with stale state.
 */
void arch_ctx_refresh(Arch_Thread_Context* ctx);
/** Update ctx TLS base (FS_BASE) and program the live CPU when on this thread.
 */
void arch_set_user_tls_base(Arch_Thread_Context* ctx, u64 tls_base);
static inline u64 arch_get_user_tls_base(const Arch_Thread_Context* ctx)
{
        return ctx ? ctx->user_fs : 0;
}
/**
 * Return-to-user have two path (Path A vs Path B)
 *
 * Path A — same thread already inside a syscall/trap and want to change the exit info:
 *   rewrite the live @c trap_frame with @c arch_syscall_set_user_return
 *   (optional @c arch_syscall_set_user_int_arg ). Hardware exit ( @c eret ) reloads that same frame.
 *   Do not call @c arch_return_to_user afterward.
 *   Examples: execve replace-image, signal deliver onto the current frame.
 *
 * Path B — no usable syscall frame (new thread / first drop):
 *   prepare a frame under @c kstack_bottom (empty or copy), and using
 *   @c arch_syscall_set_user_return, then @c arch_return_to_user → drop.
 *   Examples: PID1 first entry, @c run_elf_program, @c run_copied_thread.
 */
/**
 * @brief Path B: if have template_tf, install user return state under @p kstack_bottom 
 *        or using the exist trapframe under @p kstack_bottom
 *        and  leave the kernel via @c arch_drop_to_user.
 * @param kstack_bottom Thread kernel stack high address; no-op if 0.
 * @param template_tf If non-NULL, copied onto @c ((trap_frame*)kstack_bottom)-1
 *        (after zeroing); if NULL, uses the frame already there.
 * @param syscall_ret Written to @c rax before drop.
 * @note Does not return on success. Path A must not use this.
 */
void arch_return_to_user(u64 kstack_bottom,
                         const struct trap_frame* template_tf, u64 syscall_ret);
/**
 * @brief Commit user PC / SP / syscall return onto a live frame (Path A or B).
 *
 * Writes @c tf->rcx = PC, @c tf->rax = ret, @c percpu(user_rsp_scratch) = SP
 * (and @c ctx->user_rsp if @p ctx non-NULL).
 * Path A: do not call @c arch_return_to_user afterward—@c sysretq uses this
 * frame. Path B: call this, then @c arch_return_to_user.
 *
 * @param tf target trap_frame (Path A: in-flight syscall_ctx)
 * @param ctx Optional; updated user_rsp when non-NULL
 * @param user_pc User RIP loaded into rcx for sysret
 * @param user_sp User RSP via scratch (not tf->rsp)
 * @param syscall_ret Value restored from the rax stack slot on exit
 */
void arch_syscall_set_user_return(struct trap_frame* tf, Arch_Thread_Context* ctx,
                                  vaddr user_pc, vaddr user_sp,
                                  u64 syscall_ret);
/**
 * @brief Read back user PC / SP / ret from a live frame.
 * Opposite of @c arch_syscall_set_user_return.
 */
void arch_syscall_get_user_return(const struct trap_frame* tf,
                                  const Arch_Thread_Context* ctx, vaddr* user_pc,
                                  vaddr* user_sp, u64* syscall_ret);
/**
 * @brief Set SysV AMD64 user int arg (0=rdi … 5=r9) on @p tf.
 *
 * Complements @c arch_syscall_set_user_return (PC/SP/ret only). Used when the
 * user entry needs ABI args (e.g. signal: arg0 = sig).
 *
 * @note Path A + @c set_user_return: do not touch arg_index 3 (rcx = return
 *       PC). Prefer calling after @c set_user_return for args that do not
 *       overlap (arg0–2, 4–5).
 */
void arch_syscall_set_user_int_arg(struct trap_frame* tf,
                                   unsigned int arg_index, u64 value);

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
static inline void arch_task_ctx_init(Arch_Thread_Context* ctx)
{
        ctx->rsp = ctx->stack_bottom = ctx->user_rsp = 0;
        ctx->rbp = ctx->rbx = 0;
        ctx->r15 = ctx->r14 = 0;
        ctx->r13 = ctx->r12 = 0;
        ctx->user_gs = ctx->user_fs = 0;
}
/**
 * @brief set first-time kernel entry context: push return to @p func_ptr on the
 *        kstack and set @c ctx->rsp / @c stack_bottom.
 * @param reserve_trap_frame If true, reserve a @c trap_frame space below
 *        @p kstack_bottom before the RFLAGS/return-address pair.
 *
 * [中文临时对照 — 审阅后可删]
 * 首次内核入口上下文：在 kstack 上压入返回到 @p func_ptr，并设置
 * @c ctx->rsp / @c stack_bottom。
 * @param reserve_trap_frame 为真时，在 RFLAGS/返回地址对之前于
 *        @p kstack_bottom 下预留一个 @c trap_frame 槽。
 */
static inline void arch_set_new_thread_ctx(Arch_Thread_Context* ctx,
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
static inline vaddr arch_get_thread_user_sp(Arch_Thread_Context* ctx)
{
        return ctx->user_rsp;
}
static inline void arch_set_thread_user_sp(Arch_Thread_Context* ctx,
                                           vaddr user_sp)
{
        ctx->user_rsp = user_sp;
};
/**
 * @brief Assembly: save/restore callee-saved regs + kernel SP (see
 *        arch_switch.S). Called only from @c switch_to.
 */
extern void context_switch(Arch_Thread_Context* old_context,
                           Arch_Thread_Context* new_context);

/**
 * @brief Arch context switch after @c schedule unlocks @c sched_lock.
 *
 * Saves TSS.RSP0, KERNEL_GS_BASE, FS_BASE, and per-CPU user_rsp_scratch into
 * @p old_context; loads the same from @p new_context; then @c context_switch.
 * Does not write CR3 / user page-table root (handled by schedule).
 */
void switch_to(Arch_Thread_Context* old_context, Arch_Thread_Context* new_context);
/** @brief using @p tf to drop to user space.
 */
void arch_drop_to_user(struct trap_frame* tf);
#endif