#include <arch/aarch64/tcb_arch.h>
#include <arch/aarch64/sys_ctrl.h>

extern void arch_init_drop_to_user(vaddr user_kstack_bottom, vaddr entry);
void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        mrs("TPIDR_EL0", old_context->tpidr_el0);
        msr("TPIDR_EL0", new_context->tpidr_el0);
        mrs("SP_EL0", old_context->sp_el0);
        msr("SP_EL0", new_context->sp_el0);
        context_switch(old_context, new_context);
}
void arch_drop_to_user(vaddr user_kstack_bottom, vaddr entry)
{
        arch_init_drop_to_user(user_kstack_bottom, entry);
}