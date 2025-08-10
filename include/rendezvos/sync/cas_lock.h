#ifndef _RENDEZVOS_CAS_SPIN_LOCK_H_
#define _RENDEZVOS_CAS_SPIN_LOCK_H_
#include <common/stddef.h>
#include <common/types.h>
#include "atomic.h"
#include "barrier.h"

typedef u64 cas_lock_t;

static inline void lock_init_cas(cas_lock_t *cas_lock){
    *cas_lock = 0;
    barrier();
}
static inline void lock_cas(cas_lock_t *cas_lock){
    u64 expected = 0;
    u64 desired = 1;
    while( atomic64_cas((volatile u64*)cas_lock,*(u64*)&expected,desired)==0){
        expected = 0;
        arch_cpu_relax();
    }
}
static inline void unlock_cas(cas_lock_t* cas_lock){
    atomic64_store(cas_lock,0);
}
static inline u64 trylock_cas(cas_lock_t* cas_lock){
    u64 expected = 0;
    u64 desired = 1;
    return atomic64_cas((volatile u64*)cas_lock,*(u64*)&expected,desired);
}

#endif