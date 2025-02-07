/*
 *  This file is used to take the place of std lib <limits.h>,as we use no std
 */
#ifndef _SHAMPOOS_LIMIT_H_
#define _SHAMPOOS_LIMIT_H_
#include <common/stddef.h>
#define SHAMPOOS_MAX_MEMORY_REGIONS 128

#ifndef NR_CPUS
#define SHAMPOOS_MAX_CPU_NUMBER 128
#else
#define SHAMPOOS_MAX_CPU_NUMBER MIN(NR_CPUS, 128)
#endif
#endif