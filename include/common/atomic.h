#ifndef _RENDEZVOS_ATOMIC_H_
#define _RENDEZVOS_ATOMIC_H_
/*
 * 64-bit atomics with architecture-specific implementations.
 * we do not try use gcc Built-in atomic functions:
 * builtin atomic see
 * https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
 */
#include "types.h"

/**
 * @brief 32-bit atomic counter type
 */
typedef struct {
        volatile i32 counter;
} atomic_t;

/**
 * @brief 64-bit atomic counter type
 */
typedef struct {
        volatile i64 counter;
} atomic64_t;

/**
 * @brief Compare-and-swap on a raw u64 cell.
 * @return Previous u64 value at @p addr (equals @p expected on success)
 */
u64 atomic64_cas(volatile u64 *addr, u64 expected, u64 desired);

/**
 * @brief Atomic exchange; returns previous u64 value.
 */
u64 atomic64_exchange(volatile u64 *addr, u64 newval);
u64 atomic64_load(volatile const u64 *ptr);
void atomic64_store(volatile u64 *ptr, u64 value);
void atomic64_init(atomic64_t *ptr, i64 value);
void atomic64_add(atomic64_t *ptr, i64 value);
void atomic64_sub(atomic64_t *ptr, i64 value);
i64 atomic64_fetch_add(atomic64_t *ptr, i64 value);
i64 atomic64_fetch_sub(atomic64_t *ptr, i64 value);
void atomic64_inc(atomic64_t *ptr);
void atomic64_dec(atomic64_t *ptr);
i64 atomic64_fetch_inc(atomic64_t *ptr);
i64 atomic64_fetch_dec(atomic64_t *ptr);

#endif