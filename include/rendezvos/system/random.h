#ifndef _RENDEZVOS_SYSTEM_RANDOM_H_
#define _RENDEZVOS_SYSTEM_RANDOM_H_

/**
 * @file random.h
 * @brief Kernel random API (hardware-backed when available; per-CPU state).
 *
 * Layering:
 * - Arch: @c arch_get_random_long() in @c include/arch/<isa>/random.h
 *   (x86_64 RDRAND, aarch64 RNDR). Returns false if HW missing/fails.
 * - Software step: **only** @c splitmix64() in @c common/rand.h.
 * - This module: per-CPU state; @c random_cpu_init() / @c rand64() / @c rand32().
 *
 * Boot: @c random_cpu_init() once per CPU after @c arch_enable_percpu and
 * @c arch_start_core (@c cmain / @c start_secondary_cpu).
 */

#include <common/types.h>

#ifdef _AARCH64_
#include <arch/aarch64/random.h>
#elif defined(_X86_64_)
#include <arch/x86_64/random.h>
#else
#include <common/stdbool.h>
static inline bool arch_get_random_long(u64 *val)
{
        (void)val;
        return false;
}
#endif

/**
 * @brief Seed this CPU's random state (once per CPU after core start).
 */
void random_cpu_init(void);

/**
 * @brief Next u64 from this CPU's stream (never fails).
 *
 * Requires @c random_cpu_init() on this CPU. Thread context only (per-CPU
 * slot is plain RMW — not IRQ-reentrant on the same CPU).
 */
u64 rand64(void);

/**
 * @brief 32-bit value from one @c rand64(): high half XOR low half.
 */
static inline u32 rand32(void)
{
        u64 word = rand64();

        return (u32)(word ^ (word >> 32));
}

#endif /* _RENDEZVOS_SYSTEM_RANDOM_H_ */
