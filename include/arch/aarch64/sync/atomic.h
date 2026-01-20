#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>
#include "barrier.h"

static inline u64 atomic64_cas(volatile u64 *addr, u64 expected, u64 newval)
{
        u64 oldval;
        u64 result;
        dmb(ISH);

        __asm__ volatile("1: ldaxr %0, [%2]\n"
                         "   cmp %0, %3\n"
                         "   b.ne atomic64_cas_end\n"
                         "   stlxr %w1, %4, [%2]\n"
                         "   cbnz %w1, 1b\n"
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
        __asm__ volatile("1: ldaxr %0, [%2]\n"
                         "   stlxr %w1, %3, [%2]\n"
                         "   cbnz %w1, 1b\n"
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
static inline void atomic64_init(atomic64_t *ptr, i64 value)
{
        atomic64_store((volatile u64 *)&ptr->counter, (u64)value);
}
static inline void atomic64_add(atomic64_t *ptr, i64 value)
{
        i64 tmp;
        i64 old;

        __asm__ volatile("1: ldxr %0, [%2]\n"
                         "   add  %0, %0, %3\n"
                         "   stxr %w1, %0, [%2]\n"
                         "   cbnz %w1, 1b"
                         : "=&r"(tmp), "=&r"(old)
                         : "r"(&ptr->counter), "r"(value)
                         : "cc", "memory");
}
static inline void atomic64_sub(atomic64_t *ptr, i64 value)
{
        i64 tmp;
        i64 old;

        __asm__ volatile("1: ldxr %0, [%2]\n"
                         "   sub  %0, %0, %3\n"
                         "   stxr %w1, %0, [%2]\n"
                         "   cbnz %w1, 1b"
                         : "=&r"(tmp), "=&r"(old)
                         : "r"(&ptr->counter), "r"(value)
                         : "cc", "memory");
}
static inline i64 atomic64_fetch_add(volatile i64 *ptr, i64 value)
{
        i64 result;
        i64 tmp;

        __asm__ volatile("1: ldaxr %0, [%3]\n"
                         "   add   %1, %0, %4\n"
                         "   stlxr %w2, %1, [%3]\n"
                         "   cbnz  %w2, 1b"
                         : "=&r"(result), "=&r"(tmp), "=&r"(value)
                         : "r"(ptr), "r"(value)
                         : "cc", "memory");
        return result;
}

static inline i64 atomic64_fetch_sub(volatile i64 *ptr, i64 value)
{
        return atomic64_fetch_add(ptr, -value);
}
static inline void atomic64_inc(atomic64_t *ptr)
{
        atomic64_add(ptr, 1);
}
static inline void atomic64_dec(atomic64_t *ptr)
{
        atomic64_sub(ptr, 1);
}
static inline i64 atomic64_fetch_inc(volatile i64 *ptr)
{
        return atomic64_fetch_add(ptr, 1);
}

static inline i64 atomic64_fetch_dec(volatile i64 *ptr)
{
        return atomic64_fetch_add(ptr, -1);
}
#endif