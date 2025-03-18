#ifndef _RENDEZVOS_CPUID_H_
#define _RENDEZVOS_CPUID_H_
#include <common/stdbool.h>
/*
    there's no cpuid instruction in aarch64
    but we still need some infomation about cpu here
*/
struct cpuinfo {
        bool MP; /*multi-processor*/
        bool MT; /*multi-thread*/
};

#endif