/*
 *  This file is used to take the place of std lib <limits.h>,as we use no std
 */
#ifndef _RENDEZVOS_LIMIT_H_
#define _RENDEZVOS_LIMIT_H_
#include <common/stddef.h>
#define RENDEZVOS_MAX_MEMORY_REGIONS 128

#ifndef NR_CPUS
#define RENDEZVOS_MAX_CPU_NUMBER 128
#else
#define RENDEZVOS_MAX_CPU_NUMBER MIN(NR_CPUS, 128)
#endif
#endif