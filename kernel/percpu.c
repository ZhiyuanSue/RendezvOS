#include <shampoos/limits.h>
#include <shampoos/percpu.h>

DEFINE_PER_CPU(int, cpu_number);

void* __per_cpu_offset[SHAMPOOS_MAX_CPU_NUMBER];