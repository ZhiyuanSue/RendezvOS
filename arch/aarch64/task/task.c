#include <arch/aarch64/tcb_arch.h>

void switch_to(Arch_Task_Context* old_context, Arch_Task_Context* new_context)
{
        context_switch(old_context, new_context);
}
void arch_drop_to_user(vaddr user_sp, vaddr entry)
{
        __asm__ __volatile__("br %0" ::"r"(entry));
}