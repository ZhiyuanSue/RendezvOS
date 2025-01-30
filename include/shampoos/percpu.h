#ifndef _SHAMPOOS_PER_CPU_
#define _SHAMPOOS_PER_CPU_
#include <common/types.h>
#include "limits.h"

#define PER_CPU_SECTION ".percpu..data"

#define DEFINE_PER_CPU(type, name) \
        __attribute__((section(PER_CPU_SECTION))) __typeof__(type) name

extern u64 __per_cpu_offset[SHAMPOOS_MAX_CPU_NUMBER];
#define per_cpu_offset(x) (__per_cpu_offset[x])

#define per_cpu(var, cpu) \
        (*((__typeof__(var)*)((u64)(&var) + __per_cpu_offset[cpu])))
#define percpu(var) (*((__typeof__(var)*)((vaddr)(&var) + get_per_cpu_base())))
#define get_cpu_var(var)
// TODO

#define put_cpu_var(var)
// TODO

vaddr get_per_cpu_base();
void reserve_per_cpu_region(paddr* phy_kernel_end);
void calculate_per_cpu_offset();
void clean_per_cpu_region(paddr per_cpu_phy_addr);
#endif