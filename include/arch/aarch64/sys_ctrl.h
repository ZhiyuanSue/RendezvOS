#ifndef _RENDEZVOS_SYS_CTRL_H_
#define _RENDEZVOS_SYS_CTRL_H_
#include "sys_ctrl_def.h"
#include <common/types.h>

/*write reg*/
#define msr(sys_reg, value) \
        __asm__ __volatile__("msr " sys_reg ", %0;" : : "r"(value))
/*read reg*/
#define mrs(sys_reg, value) \
        __asm__ __volatile__("mrs %0, " sys_reg "\n" : "=r"(value) : :)

static inline void set_vbar_el1(vaddr trap_vec)
{
        msr("VBAR_EL1", trap_vec);
}

#endif