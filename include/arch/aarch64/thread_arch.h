#ifndef _RENDEZVOS_THREAD_ARCH_
#define _RENDEZVOS_THREAD_ARCH_

#include <common/types.h>
#include <common/string.h>
#include "sys_ctrl_def.h"
#include <arch/aarch64/trap/trap.h>
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
        u64 sp_el0;
        u64 daif;
} Arch_Thread_Context;

/* Declared in arch-specific task/arch_thread.c (context merge & syscall-trap
 * return). */
void arch_ctx_merge_from_src(Arch_Thread_Context* dst_ctx,
                             const Arch_Thread_Context* src_ctx);
/*
 * Refresh context fields from the live CPU state while running in kernel
 * during a user->kernel transition (e.g. syscall handling).
 *
 * This is the AArch64 analogue of x86_64's refresh-from-scratch/MSR logic.
 * It is intended for fork/copy performed inside syscall context so that the
 * child does not inherit stale EL0-visible state.
 */
void arch_ctx_refresh(Arch_Thread_Context* ctx);
/** Update ctx TLS base (TPIDR_EL0) and program the live CPU when on this
 * thread. */
void arch_set_user_tls_base(Arch_Thread_Context* ctx, u64 tls_base);
static inline u64 arch_get_user_tls_base(const Arch_Thread_Context* ctx)
{
        return ctx ? ctx->tpidr_el0 : 0;
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
 * @param kstack_bottom Thread kernel stack address; it must non-NULL, otherwise no-op.
 * @param template_tf If non-NULL, copied onto @c ((trap_frame*)kstack_bottom)-1
 *        (after zeroing); if NULL, uses the frame already there.
 * @param syscall_ret Written to @c REGS[0]; also sets @c tf->SP to the save
 *        area pointer before drop.
 * @note Does not return on success. Path A must not use this.
 */
void arch_return_to_user(u64 kstack_bottom,
                         const struct trap_frame* template_tf, u64 syscall_ret);
/**
 * @brief commit user PC / SP / syscall return onto a live frame (Path A or B).
 *
 * Writes @c tf->ELR = PC, @c REGS[0] = ret, @c SP_EL0 / @c ctx->sp_el0 = SP,
 * and sets @c tf->SP = (vaddr)tf (kernel save-area pointer—not user SP).
 * Path A: do not call @c arch_return_to_user afterward—@c eret uses this
 * frame. Path B: call this to set tf, then @c arch_return_to_user.
 *
 * @param tf target trap_frame (Path A: in-use syscall/trap frame)
 * @param ctx Optional; updated sp_el0 (user stack) when non-NULL
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
 * @brief Set AAPCS64 user int arg (let syscall REGS[ @p arg_index ] = @p value ).
 * the arch_syscall_set_user_return only set the PC / SP / ret, but some case
 * (like signal deliver, the sig param should be put at arg[0])
 * the trapframe should set the args, we can use this function.
 * 
 * @note If you also use the arch_syscall_set_user_return,
 * remember it will write x0 as syscall ret,
 * you should only write x0 once —— or the value will be rewrite.
 * for Path A: index 0 is also @c ARCH_SYSCALL_RET (x0). Call
 * @c set_user_int_arg(0, …) **after** @c set_user_return if the handler
 * needs x0 as an argument (overwrites the ret slot on purpose).
 */
void arch_syscall_set_user_int_arg(struct trap_frame* tf,
                                   unsigned int arg_index, u64 value);

/* Zeroed EL0-shaped frame for first ELF entry; ELR = user entry. */
static inline void arch_empty_drop_trap_frame(struct trap_frame* tf,
                                              vaddr entry_addr)
{
        memset(tf, 0, sizeof(*tf));
        tf->ELR = (u64)entry_addr;
}

typedef struct {
        void* thread_func_ptr;
        u64 int_para[NR_ABI_PARAMETER_INT_REG];
} Thread_Init_Para;
static inline void arch_task_ctx_init(Arch_Thread_Context* ctx)
{
        ctx->sp_el1 = ctx->spsr_el1 = ctx->tpidr_el0 = ctx->sp_el0 = 0;
        ctx->daif = 0;
        memset(&(ctx->regs), 0, sizeof(u64) * NR_AARCH64_CALLEE_SAVED_REGS);
}
/**
 * @brief set first-time kernel entry context: set EL1 SP, LR= @p func_ptr,
 *        SPSR.
 * @param reserve_trap_frame If true, reserve a @c trap_frame space below
 *        @p kstack_bottom , and force 16-byte SP alignment (AAPCS64).
 */
static inline void arch_set_new_thread_ctx(Arch_Thread_Context* ctx,
                                           void* func_ptr, void* kstack_bottom,
                                           bool reserve_trap_frame)
{
        vaddr bottom = (vaddr)kstack_bottom;
        vaddr sp = bottom;

        if (reserve_trap_frame) {
                sp = (vaddr)(((struct trap_frame*)bottom) - 1);
                /* AAPCS64 requires SP 16-byte aligned; trap frame size 312 is
                 * not. */
                sp &= ~(vaddr)0xfULL;
        }
        ctx->sp_el1 = (u64)sp;
        ctx->regs[aarch64_task_ctx_lr] = (u64)func_ptr;
        ctx->spsr_el1 = SPSR_EL1_M_64_EL1H;
        ctx->daif = 0;
}
static inline vaddr arch_get_thread_user_sp(Arch_Thread_Context* ctx)
{
        return ctx->sp_el0;
}
static inline void arch_set_thread_user_sp(Arch_Thread_Context* ctx,
                                           vaddr user_sp)
{
        ctx->sp_el0 = user_sp;
};
/**
 * @brief Assembly: save/restore x19–x30, SP, SPSR_EL1 (see arch_switch.S).
 *        Called only from @c switch_to.
 */
extern void context_switch(Arch_Thread_Context* old_context,
                           Arch_Thread_Context* new_context);

/**
 * @brief Arch context switch after @c schedule unlocks @c sched_lock.
 *
 * Saves/loads TPIDR_EL0 and SP_EL0; saves DAIF into @p old_context before
 * @c context_switch; after return to old context, restores DAIF.
 * Does not change TTBR0( handled by @c schedule ) 
 */
void switch_to(Arch_Thread_Context* old_context, Arch_Thread_Context* new_context);
/** @brief using @p tf to drop to user space.
 */
void arch_drop_to_user(struct trap_frame* tf);
#endif