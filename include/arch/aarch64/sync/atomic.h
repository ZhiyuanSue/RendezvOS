#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>
#include "barrier.h"
#include <modules/log/log.h>

static inline uint64_t atomic64_cas(volatile uint64_t *addr, uint64_t expected,
                                    uint64_t newval)
{
        uint64_t oldval;
        uint64_t result;
        dsb(SY);

        __asm__ volatile(
                "atomic64_cas: ldxr %0, [%2]\n"
                "   cmp %0, %3\n"
                "   b.ne atomic64_cas_end\n"
                "   stxr %w1, %4, [%2]\n"
                "   cbnz %w1, atomic64_cas\n"
                "atomic64_cas_end:"
                : "=&r"(oldval), "=&r"(result)
                : "r"(addr), "r"(expected), "r"(newval)
                : "memory", "cc");
        dsb(SY);
        return oldval;
}

static inline uint64_t atomic64_exchange(volatile uint64_t *addr,
                                         uint64_t newval)
{
        uint64_t oldval, result;
        dsb(SY);
        __asm__ volatile("atomic64_exchange: ldxr %0, [%2]\n"
                         "   stxr %w1, %3, [%2]\n"
                         "   cbnz %w1, atomic64_exchange\n"
                         : "=&r"(oldval), "=&r"(result)
                         : "r"(addr), "r"(newval)
                         : "memory");
        dsb(SY);
        return oldval;
}

#endif