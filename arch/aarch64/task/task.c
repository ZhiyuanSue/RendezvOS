#include <arch/aarch64/tcb_arch.h>
#include <arch/aarch64/sys_ctrl.h>

void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        mrs("TPIDR_EL0", old_context->tpidr_el0);
        msr("TPIDR_EL0", new_context->tpidr_el0);
        context_switch(old_context, new_context);
}
void arch_drop_to_user(vaddr user_kstack_bottom, vaddr user_sp, vaddr entry)
{
        __asm__ __volatile__("br %0" ::"r"(entry));
}