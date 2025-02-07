#include <arch/aarch64/sys_ctrl.h>
#include <arch/aarch64/trap/trap.h>

extern u64 trap_vec_table;
void arch_init_interrupt(void)
{
        set_vbar_el1((vaddr)(&trap_vec_table));
}
void arch_unknown_trap_handler(struct trap_frame *tf)
{
        /*print the trap frames*/
}