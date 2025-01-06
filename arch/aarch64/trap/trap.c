#include <arch/aarch64/sys_ctrl.h>
#include <arch/x86_64/trap/trap.h>

extern u64 trap_vec_table;
void init_interrupt(void)
{
        set_vbar_el1((vaddr)(&trap_vec_table));
}