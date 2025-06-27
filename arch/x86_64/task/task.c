#include <arch/x86_64/tcb_arch.h>
#include <rendezvos/smp/percpu.h>
#include <arch/x86_64/desc.h>
#include <arch/x86_64/trap/tss.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/msr.h>
extern struct TSS cpu_tss;
DEFINE_PER_CPU(vaddr, user_rsp_scratch);
void arch_init_drop_to_user(vaddr user_kstack_bottom, vaddr user_sp,
                            vaddr entry);
void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        /*change the new tss rsp0*/
        old_context->stack_bottom = get_rsp(&percpu(cpu_tss), 0);
        set_rsp(&percpu(cpu_tss), 0, new_context->stack_bottom);
        /*save and change the new MSR_KERNEL_GS_BASE*/
        old_context->user_gs = rdmsr(MSR_KERNEL_GS_BASE);
        wrmsr(MSR_KERNEL_GS_BASE, new_context->user_gs);
        /*also save the fs*/
        old_context->user_fs = rdmsr(MSR_FS_BASE);
        wrmsr(MSR_FS_BASE, new_context->user_fs);
        context_switch(old_context, new_context);
}
void arch_drop_to_user(vaddr user_kstack_bottom, vaddr user_sp, vaddr entry)
{
        /*we have to build a syscall context for exit*/
        arch_init_drop_to_user(user_kstack_bottom, user_sp, entry);
}