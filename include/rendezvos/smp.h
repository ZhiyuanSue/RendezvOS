#ifndef _RENDEZVOS_SMP_H_
#define _RENDEZVOS_SMP_H_

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

void start_smp(struct setup_info *arch_setup_info);

#endif