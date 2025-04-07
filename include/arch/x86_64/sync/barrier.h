#ifndef _RENDEZVOS_BARRIER_H_
#define _RENDEZVOS_BARRIER_H_
#include <common/barrier.h>
#define sfence() __asm__ __volatile__("sfence" : : : "memory")
#define lfence() __asm__ __volatile__("lfence" : : : "memory")
#define mfence() __asm__ __volatile__("mfence" : : : "memory")
#endif