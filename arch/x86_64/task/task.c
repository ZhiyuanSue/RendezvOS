#include <arch/x86_64/tcb_arch.h>
void arch_drop_to_user(vaddr user_sp, vaddr entry)
{
        __asm__ __volatile__("jmp %0" ::"r"(entry));
}