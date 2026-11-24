#ifndef _RENDEZVOS_CPUID_H_
#define _RENDEZVOS_CPUID_H_

#include <common/types.h>
#include <arch/aarch64/sys_ctrl_def.h>

/*
    there's no cpuid instruction in aarch64
    but we still need some infomation about cpu here
 */
struct cpuinfo {
        union {
                u64 mpidr_el1_val;
                struct {
                        u64 aff0 : 8;
                        u64 aff1 : 8;
                        u64 aff2 : 8;
                        u64 mt : 1;
                        u64 res0_25_29 : 5;
                        u64 u : 1;
                        u64 res0_31 : 1;
                        u64 aff3 : 8;
                        u64 res0_40_63 : 24;
                };
        };
};

extern struct cpuinfo cpu_info;

#endif