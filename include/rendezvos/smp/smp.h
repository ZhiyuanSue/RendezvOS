#ifndef _RENDEZVOS_SMP_H_
#define _RENDEZVOS_SMP_H_

#include <common/stdbool.h>
#include <common/types.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>

#ifdef _AARCH64_
#include <arch/aarch64/smp.h>
#elif defined _X86_64_
#include <arch/x86_64/smp.h>
#else
#include <arch/x86_64/smp.h>
#endif

/**
 * @brief Per-CPU bring-up state (stored in @c DEFINE_PER_CPU(CPU_STATE)).
 */
enum cpu_status {
        no_cpu, /*no this cpu exist*/
        cpu_disable, /*this cpu is exist but not enable*/
        cpu_enable, /*cpu is exist and enable*/
};

/**
 * @brief per-CPU slot symbol; use @c per_cpu ( CPU_STATE, id ) / @c percpu (
 * CPU_STATE ).
 */
extern volatile u64 CPU_STATE;

/**
 * @brief True if @p cpu_id CPU has @c CPU_STATE == @c cpu_enable
 */
static inline bool cpu_is_online(cpu_id_t cpu_id)
{
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return false;
        return per_cpu(CPU_STATE, cpu_id) == (u64)cpu_enable;
}

/**
 * @brief BSP: wake APs
 *
 * @param arch_setup_info Boot setup block (AP stack / cpu_id fields updated
 *        by @c arch_start_smp before each wake)
 */
void start_smp(struct setup_info *arch_setup_info);

#endif