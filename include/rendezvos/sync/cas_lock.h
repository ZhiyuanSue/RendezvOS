#ifndef _RENDEZVOS_CAS_SPIN_LOCK_H_
#define _RENDEZVOS_CAS_SPIN_LOCK_H_
#include <common/stddef.h>
#include <common/types.h>
#include <common/atomic.h>
#include "barrier.h"

/**
 * @brief Single-word test-and-set lock (0 = free, 1 = held).
 */
typedef u64 cas_lock_t;

/**
 * @brief Initialize @p cas_lock to unlocked (0).
 */
static inline void lock_init_cas(cas_lock_t* cas_lock)
{
        *cas_lock = 0;
        barrier();
}

/**
 * @brief Acquire a CAS lock (spin with arch_cpu_relax).
 *
 * For very short critical sections only. Must not schedule while held.
 */
static inline void lock_cas(cas_lock_t* cas_lock)
{
        while (atomic64_exchange((volatile u64*)cas_lock, 1) == 1) {
                arch_cpu_relax();
        }
}

/**
 * @brief Release: store 0.
 */
static inline void unlock_cas(cas_lock_t* cas_lock)
{
        atomic64_store(cas_lock, 0);
}

/**
 * @brief Try once: exchange to 1.
 * @return Previous value (0 = acquired, 1 = was busy)
 */
static inline u64 trylock_cas(cas_lock_t* cas_lock)
{
        return atomic64_exchange((volatile u64*)cas_lock, 1);
}

#endif