#ifndef _SHAMPOOS_BARRIER_H_
#define _SHAMPOOS_BARRIER_H_

#define barrier() asm volatile("" : : : "memory")
#define sfence()  asm volatile("sfence" : : : "memory")
#define lfence()  asm volatile("lfence" : : : "memory")
#define mfence()  asm volatile("mfence" : : : "memory")
#endif