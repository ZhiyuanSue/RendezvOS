#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>

static inline uint64_t atomic64_cas(volatile uint64_t *addr, uint64_t expected,
                              uint64_t desired)
{
        uint64_t result;
        __asm__ __volatile__("casal %w1, %w2, [%0]"
                     : "=r"(result)
                     : "r"(addr), "r"(expected), "r"(desired)
                     : "memory");
        return result;
}

static inline uint64_t atomic64_exchange(volatile uint64_t *addr, uint64_t newval)
{
        uint64_t oldval, status;
        do {
                __asm__ __volatile__("ldxr %w0, [%1]\n"
                             "stxr %w0, %w2, [%1]"
                             : "=&r"(oldval), "=&r"(status)
                             : "r"(addr), "r"(newval)
                             : "memory");
        } while (status != 0);
        return oldval;
}

#endif