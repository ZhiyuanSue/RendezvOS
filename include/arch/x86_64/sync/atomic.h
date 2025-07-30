#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>
#include "barrier.h"
static inline uint64_t atomic64_cas(volatile uint64_t *addr, uint64_t expected,
                                    uint64_t desired)
{
        uint64_t result;
        __asm__ __volatile__("lock; cmpxchgq %2, %1\n"
                             : "=a"(result), "+m"(*addr)
                             : "r"(desired), "0"(expected)
                             : "memory", "cc");
        return result;
}
static inline uint64_t atomic64_exchange(volatile uint64_t *addr,
                                         uint64_t newval)
{
        uint64_t oldval;
        __asm__ __volatile__("lock xchgq %1, %0"
                             : "=r"(oldval), "+m"(*addr)
                             : "0"(newval)
                             : "memory");
        return oldval;
}

static inline u64 atomic64_load(volatile const u64* ptr)
{
        u64 value = *ptr;
        barrier();
        return value;
}

static inline void atomic64_store(volatile u64* ptr,u64 value)
{
        barrier();
        *ptr=value;
}
#endif