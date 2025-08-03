#ifndef _RENDEZVOS_PER_CPU_
#define _RENDEZVOS_PER_CPU_
#include <common/types.h>
#include <rendezvos/limits.h>

#define PER_CPU_SECTION ".percpu..data"

#define DEFINE_PER_CPU(type, name) \
        __attribute__((section(PER_CPU_SECTION))) __typeof__(type) name

extern char _per_cpu_end, _per_cpu_start;
extern u64 __per_cpu_offset[RENDEZVOS_MAX_CPU_NUMBER];
extern int cpu_number;
#define per_cpu_offset(x) (__per_cpu_offset[x])

#define per_cpu(var, cpu)                                            \
        (*((__typeof__(var)*)(((u64)(&var) - (vaddr)&_per_cpu_start) \
                              + __per_cpu_offset[cpu])))
#define percpu(var)                                                    \
        (*((__typeof__(var)*)(((vaddr)(&var) - (vaddr)&_per_cpu_start) \
                              + get_per_cpu_base())))
#define get_cpu_var(var)
// TODO

#define put_cpu_var(var)
// TODO

vaddr get_per_cpu_base();
void reserve_per_cpu_region(paddr* phy_kernel_end);
void calculate_per_cpu_offset();
void clean_per_cpu_region(paddr per_cpu_phy_addr);
#endif