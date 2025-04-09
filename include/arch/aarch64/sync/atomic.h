#ifndef _RENDEZVOS_ARCH_ATOMIC_H_
#define _RENDEZVOS_ARCH_ATOMIC_H_
#include <common/types.h>
#include <common/stdbool.h>
#include "barrier.h"

static inline uint64_t atomic64_cas(volatile uint64_t *addr, uint64_t expected,
                                    uint64_t desired)
{
        uint64_t oldval;
        uint64_t result;
        dsb(SY);

        __asm__ volatile(
                "1: ldxr    %x[old], [%[addr]]\n" // 独占加载内存值到
                                                  // old_val（64位）
                "   cmp     %x[old], %x[expected]\n" // 比较当前值和预期值（64位）
                "   cset    %x[result], eq\n" // 如果相等，result=1；否则
                                              // result=0
                "   stxr    %w[result], %x[new], [%[addr]]\n" // 尝试存储新值（64位）
                "   cbnz    %w[result], 1b" // 如果存储失败，重试
                : [old] "=r"(oldval), [result] "=r"(result)
                : [addr] "r"(addr), [expected] "r"(expected), [new] "r"(desired)
                : "memory", "cc");
        dsb(SY);
        return oldval;
}

static inline uint64_t atomic64_exchange(volatile uint64_t *addr,
                                         uint64_t newval)
{
        uint64_t oldval, result;
        dsb(SY);
        __asm__ volatile (
			"1: ldxr    %x[old], [%[addr]]\n"   // 独占加载内存值到 old_val
			"   stxr    %w[result], %x[new], [%[addr]]\n" // 尝试存储新值
			"   cbnz    %w[result], 1b"         // 如果存储失败，重试
			: [old] "=r" (oldval), [result] "=r" (result)
			: [addr] "r" (addr), [new] "r" (newval)
			: "memory", "cc"
		);
		dsb(SY);
        return oldval;
}

#endif