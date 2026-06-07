#include <arch/aarch64/tcb_arch.h>
#include <arch/aarch64/sys_ctrl.h>
#include <common/string.h>

void arch_ctx_refresh(Arch_Task_Context* ctx)
{
        if (!ctx) {
                return;
        }

        /* Refresh user-visible state from EL1 system registers. */
        mrs("SP_EL0", ctx->sp_el0);
        mrs("TPIDR_EL0", ctx->tpidr_el0);
}

void arch_set_user_tls_base(Arch_Task_Context* ctx, u64 tls_base)
{
        if (!ctx) {
                return;
        }
        ctx->tpidr_el0 = tls_base;
        msr("TPIDR_EL0", tls_base);
}

void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        mrs("TPIDR_EL0", old_context->tpidr_el0);
        msr("TPIDR_EL0", new_context->tpidr_el0);
        mrs("SP_EL0", old_context->sp_el0);
        msr("SP_EL0", new_context->sp_el0);
        context_switch(old_context, new_context);
}

void arch_ctx_merge_from_src(Arch_Task_Context* dst_ctx,
                             const Arch_Task_Context* src_ctx)
{
        u64 sp_el1;
        u64 lr;

        if (!dst_ctx || !src_ctx) {
                return;
        }
        sp_el1 = dst_ctx->sp_el1;
        lr = dst_ctx->regs[aarch64_task_ctx_lr];
        *dst_ctx = *src_ctx;
        dst_ctx->sp_el1 = sp_el1;
        dst_ctx->regs[aarch64_task_ctx_lr] = lr;
}

void arch_syscall_set_user_return(struct trap_frame* tf, Arch_Task_Context* ctx,
                                  vaddr user_pc, vaddr user_sp, u64 syscall_ret)
{
        if (!tf) {
                return;
        }
        tf->ELR = user_pc;
        tf->REGS[0] = syscall_ret;
        tf->SP = (vaddr)tf;
        if (ctx) {
                ctx->sp_el0 = user_sp;
        }
        msr("SP_EL0", user_sp);
}

void arch_syscall_get_user_return(const struct trap_frame* tf,
                                  const Arch_Task_Context* ctx, vaddr* user_pc,
                                  vaddr* user_sp, u64* syscall_ret)
{
        (void)ctx;
        if (user_pc) {
                *user_pc = tf ? tf->ELR : 0;
        }
        if (user_sp) {
                u64 sp_el0 = 0;
                mrs("SP_EL0", sp_el0);
                *user_sp = (vaddr)sp_el0;
        }
        if (syscall_ret) {
                *syscall_ret = tf ? tf->REGS[0] : 0;
        }
}

void arch_syscall_set_user_int_arg(struct trap_frame* tf, unsigned int arg_index,
                                   u64 value)
{
        if (!tf || arg_index >= NR_ABI_PARAMETER_INT_REG) {
                return;
        }
        tf->REGS[arg_index] = value;
}

void arch_return_to_user(u64 kstack_bottom,
                         const struct trap_frame* template_tf, u64 syscall_ret)
{
        struct trap_frame* dst_tf;

        if (!kstack_bottom) {
                return;
        }

        dst_tf = ((struct trap_frame*)kstack_bottom) - 1;
        if (template_tf) {
                memset(dst_tf, 0, sizeof(struct trap_frame));
                memcpy(dst_tf, template_tf, sizeof(struct trap_frame));
        }
        dst_tf->REGS[0] = syscall_ret;
        dst_tf->SP = (u64)dst_tf;

        arch_drop_to_user(dst_tf);
}
