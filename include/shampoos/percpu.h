#ifndef _SHAMPOOS_PER_CPU_
#define _SHAMPOOS_PER_CPU_
#include "limits.h"

#define PER_CPU_SECTION ".data..percpu"

#define DEFINE_PER_CPU(type, name) \
        __attribute__((section(PER_CPU_SECTION))) __typeof__(type) name

extern void* __per_cpu_offset[SHAMPOOS_MAX_CPU_NUMBER];
#define per_cpu_offset(x) (__per_cpu_offset[x])

#define get_cpu_var(var)
// TODO

#define put_cpu_var(var)
// TODO

#endif