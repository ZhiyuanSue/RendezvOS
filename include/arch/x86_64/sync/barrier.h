#ifndef _SHAMPOOS_BARRIER_H_
#define _SHAMPOOS_BARRIER_H_

#define barrier() __asm__ __volatile__("" : : : "memory")
#define sfence()  __asm__ __volatile__("sfence" : : : "memory")
#define lfence()  __asm__ __volatile__("lfence" : : : "memory")
#define mfence()  __asm__ __volatile__("mfence" : : : "memory")
#endif