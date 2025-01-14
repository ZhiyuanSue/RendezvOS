#ifndef _SHAMPOOS_PERCPU_H_
#define _SHAMPOOS_PERCPU_H_

#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>
static inline vaddr get_per_cpu_base()
{
        vaddr addr;
        mrs("TPIDR_EL1", addr);
        return addr;
}

#endif