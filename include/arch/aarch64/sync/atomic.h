#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>
#include "barrier.h"
#include <modules/log/log.h>

static inline u64 atomic64_cas(volatile u64 *addr, u64 expected, u64 newval)
{
        u64 oldval;
        u64 result;
        dmb(ISH);

        __asm__ volatile("atomic64_cas_start: ldaxr %0, [%2]\n"
                         "   cmp %0, %3\n"
                         "   b.ne atomic64_cas_end\n"
                         "   stlxr %w1, %4, [%2]\n"
                         "   cbnz %w1, atomic64_cas_start\n"
                         "atomic64_cas_end:"
                         : "=&r"(oldval), "=&r"(result)
                         : "r"(addr), "r"(expected), "r"(newval)
                         : "memory", "cc");
        dmb(ISH);
        return oldval;
}

static inline u64 atomic64_exchange(volatile u64 *addr, u64 newval)
{
        u64 oldval, result;
        dmb(ISH);
        __asm__ volatile("atomic64_exchange: ldaxr %0, [%2]\n"
                         "   stlxr %w1, %3, [%2]\n"
                         "   cbnz %w1, atomic64_exchange\n"
                         : "=&r"(oldval), "=&r"(result)
                         : "r"(addr), "r"(newval)
                         : "memory");
        dmb(ISH);
        return oldval;
}

static inline u64 atomic64_load(volatile const u64 *ptr)
{
        u64 value = *ptr;
        barrier();
        dmb(ISH);
        return value;
}

static inline void atomic64_store(volatile u64 *ptr, u64 value)
{
        barrier();
        dmb(ISH);
        *ptr = value;
}
#endif