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
