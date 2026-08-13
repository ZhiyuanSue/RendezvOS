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
enum cpu_status {
        no_cpu, /*no this cpu exist*/
        cpu_disable, /*this cpu is exist but not enable*/
        cpu_enable, /*cpu is exist and enable*/
};
extern volatile u64 CPU_STATE;

static inline bool cpu_is_online(cpu_id_t cpu_id)
{
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return false;
        return per_cpu(CPU_STATE, cpu_id) == (u64)cpu_enable;
}

void start_smp(struct setup_info *arch_setup_info);

#endif