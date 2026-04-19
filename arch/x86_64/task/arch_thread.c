#include <arch/x86_64/tcb_arch.h>
#include <rendezvos/smp/percpu.h>
#include <arch/x86_64/desc.h>
#include <arch/x86_64/trap/tss.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/msr.h>
#include <common/string.h>
#include <common/types.h>

extern struct TSS cpu_tss;
DEFINE_PER_CPU(vaddr, user_rsp_scratch);

void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        old_context->stack_bottom = get_rsp(&percpu(cpu_tss), 0);
        set_rsp(&percpu(cpu_tss), 0, new_context->stack_bottom);
        old_context->user_gs = rdmsrq(MSR_KERNEL_GS_BASE);
        wrmsrq(MSR_KERNEL_GS_BASE, new_context->user_gs);
        old_context->user_fs = rdmsrq(MSR_FS_BASE);
        wrmsrq(MSR_FS_BASE, new_context->user_fs);
        old_context->user_rsp = percpu(user_rsp_scratch);
        percpu(user_rsp_scratch) = new_context->user_rsp;
        context_switch(old_context, new_context);
}

void arch_ctx_merge_from_src(Arch_Task_Context* dst_ctx,
                             const Arch_Task_Context* src_ctx)
{
        if (!dst_ctx || !src_ctx) {
                return;
        }
        dst_ctx->r15 = src_ctx->r15;
        dst_ctx->r14 = src_ctx->r14;
        dst_ctx->r13 = src_ctx->r13;
        dst_ctx->r12 = src_ctx->r12;
        dst_ctx->rbp = src_ctx->rbp;
        dst_ctx->rbx = src_ctx->rbx;
        dst_ctx->user_gs = src_ctx->user_gs;
        dst_ctx->user_fs = src_ctx->user_fs;
        dst_ctx->user_rsp = src_ctx->user_rsp;
}

void arch_return_to_user(u64 kstack_bottom,
                         const struct trap_frame* template_tf,
                         u64 syscall_ret)
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
        dst_tf->rax = syscall_ret;

        arch_drop_to_user(dst_tf);
}
