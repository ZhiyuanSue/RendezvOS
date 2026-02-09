#ifndef _RENDEZVOS_SPIN_LOCK_H_
#define _RENDEZVOS_SPIN_LOCK_H_
/*
    here we try the mcs spin-lock
    let's give some examples
    https://www.cnblogs.com/zhengsyao/p/spin_lock_scalable_spinlock.html
    https://zhuanlan.zhihu.com/p/115748853
    https://www.zhihu.com/question/55764216
    https://github.com/cyfdecyf/spinlock/ (but it use sync built-in)
    I use the last link's mcs lock version
    but use gcc built-in atomic functions rewrite it
*/
#include <common/stddef.h>
#include <common/atomic.h>
#include "barrier.h"

typedef struct spin_lock_t spin_lock_t;
struct spin_lock_t {
        spin_lock_t *next;
        u64 spin;
};
typedef struct spin_lock_t *spin_lock;

static inline void lock_mcs(spin_lock *m, spin_lock_t *me)
{
        spin_lock_t *tail;

        me->next = (spin_lock_t *)NULL;
        me->spin = 0;

        tail = (spin_lock_t *)atomic64_exchange((volatile u64 *)m, (u64)me);

        /* No one there? */
        if (!tail)
                return;

        /* Someone there, need to link in */
        tail->next = me;

        /* Make sure we do the above setting of next. */
        barrier();

        /* Spin on my spin variable */
        while (!me->spin)
                arch_cpu_relax();

        return;
}

static inline void unlock_mcs(spin_lock *m, spin_lock_t *me)
{
        /* No successor yet? */
        if (!me->next) {
                /* Try to atomically unlock */
                if (atomic64_cas((volatile u64 *)m, (u64)me, (u64)NULL)
                    == (u64)me)
                        return;

                /* Wait for successor to appear */
                while (!me->next)
                        arch_cpu_relax();
        }

        /* Unlock next one */
        me->next->spin = 1;
}

static inline int trylock_mcs(spin_lock *m, spin_lock_t *me)
{
        spin_lock_t *tail;

        me->next = (spin_lock_t *)NULL;
        me->spin = 0;

        /* Try to lock */
        tail = (spin_lock_t *)atomic64_cas(
                (volatile u64 *)m, (u64)NULL, (u64)&me);
        /* No one was there - can quickly return */
        if (!tail)
                return 0;

        return 1; // Busy
}
#endif