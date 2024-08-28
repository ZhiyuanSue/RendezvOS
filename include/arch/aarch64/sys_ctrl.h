#include "sys_ctrl_def.h"
#include <common/types.h>

static void inline set_vbar_el1(vaddr trap_vec)
{
        asm volatile("msr VBAR_EL1, %0;" : : "r"(trap_vec));
}