#ifndef _SHAMPOOS_ARCH_SMP_H_
#define _SHAMPOOS_ARCH_SMP_H_
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <shampoos/limits.h>
#include <shampoos/percpu.h>
#define _SHAMPOOS_X86_64_AP_PHY_ADDR_ 0x1000
/*
    we put the ap entry code at 0x1000 when we try to mp set up
*/
enum cpu_status {
        no_cpu, /*no this cpu exist*/
        cpu_disable, /*this cpu is exist but not enable*/
        cpu_enable, /*cpu is exist and enable*/
};

void arch_start_smp(void);
#endif