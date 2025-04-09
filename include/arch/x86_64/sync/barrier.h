#ifndef _RENDEZVOS_ARCH_BARRIER_H_
#define _RENDEZVOS_ARCH_BARRIER_H_
#define sfence() __asm__ __volatile__("sfence" : : : "memory")
#define lfence() __asm__ __volatile__("lfence" : : : "memory")
#define mfence() __asm__ __volatile__("mfence" : : : "memory")

#define arch_cpu_relax() __asm__ __volatile__("pause\n" : : : "memory")
#endif