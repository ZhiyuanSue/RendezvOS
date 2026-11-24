#ifndef _RENDEZVOS_CPU_ID_H_
#define _RENDEZVOS_CPU_ID_H_

#include <common/types.h>
#include <common/limits.h>

/** Dense software CPU index. Hardware cpu id please use @c cpu_hw_id(). */
typedef u64 cpu_id_t;

/** BSP logical id (always 0). */
extern cpu_id_t BSP_ID;

#define CPU_ID_INVALID ((cpu_id_t)U64_MAX)

#endif
