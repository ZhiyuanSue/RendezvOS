#ifndef _SHAMPOOS_X86_64_PERCPU_H_
#define _SHAMPOOS_X86_64_PERCPU_H_

#include <common/types.h>
static inline vaddr get_per_cpu_base()
{
        vaddr addr=0;
        asm volatile("mov %%gs,%0" : : "r"(addr));
        return addr;
}
#define percpu(var) (*((__typeof__(var)*)((vaddr)(&var) + get_per_cpu_base())))

#endif