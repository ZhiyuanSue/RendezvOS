/*
 *  This file is used to take the place of std lib <limits.h>,as we use no std
 */
#ifndef _RENDEZVOS_LIMIT_H_
#define _RENDEZVOS_LIMIT_H_
#include <common/stddef.h>
#include <common/types.h>
#define RENDEZVOS_MAX_MEMORY_REGIONS 128
#define RENDEZVOS_MAX_MEMORY_SIZE    0xC00000000

#ifndef NR_CPUS
#define RENDEZVOS_MAX_CPU_NUMBER 128
#else
#define RENDEZVOS_MAX_CPU_NUMBER MIN(NR_CPUS, 128)
#endif

extern u64 thread_kstack_page_num;
extern u64 thread_ustack_page_num;
#endif