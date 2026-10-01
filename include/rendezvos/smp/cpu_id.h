#ifndef _RENDEZVOS_CPU_ID_H_
#define _RENDEZVOS_CPU_ID_H_

#include <common/types.h>
#include <common/limits.h>

/**
 * @brief Software CPU index (remember in x86: often APIC id).
 */
typedef u64 cpu_id_t;

/**
 * @brief Boot-strap processor id.
 *
 * x86: rewritten from APICID in arch_cpu_info (may be non-zero).
 * aarch64: forced to 0.
 */
extern cpu_id_t BSP_ID;

#define CPU_ID_INVALID ((cpu_id_t)U64_MAX)

#endif
