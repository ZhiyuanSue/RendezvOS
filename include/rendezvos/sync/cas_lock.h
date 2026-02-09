#ifndef _RENDEZVOS_CAS_SPIN_LOCK_H_
#define _RENDEZVOS_CAS_SPIN_LOCK_H_
#include <common/stddef.h>
#include <common/types.h>
#include <common/atomic.h>
#include "barrier.h"

typedef u64 cas_lock_t;

static inline void lock_init_cas(cas_lock_t* cas_lock)
{
        *cas_lock = 0;
        barrier();
}
static inline void lock_cas(cas_lock_t* cas_lock)
{
        while (atomic64_exchange((volatile u64*)cas_lock, 1) == 1) {
                arch_cpu_relax();
        }
}
static inline void unlock_cas(cas_lock_t* cas_lock)
{
        atomic64_store(cas_lock, 0);
}
static inline u64 trylock_cas(cas_lock_t* cas_lock)
{
        return atomic64_exchange((volatile u64*)cas_lock, 1);
}

#endif