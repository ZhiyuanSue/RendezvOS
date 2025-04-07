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
#include <common/spin.h>
#include <common/barrier.h>

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

        __atomic_exchange(m, &me, &tail, __ATOMIC_SEQ_CST);

        /* No one there? */
        if (!tail)
                return;

        /* Someone there, need to link in */
        tail->next = me;

        /* Make sure we do the above setting of next. */
        barrier();

        /* Spin on my spin variable */
        while (!me->spin)
                cpu_idle();

        return;
}

static inline void unlock_mcs(spin_lock *m, spin_lock_t *me)
{
        /* No successor yet? */
        if (!me->next) {
                /* Try to atomically unlock */
                // if (cmpxchg(m, me, NULL) == me)
                //         return;
                if (__atomic_compare_exchange_n(m,
                                                &me,
                                                NULL,
                                                false,
                                                __ATOMIC_SEQ_CST,
                                                __ATOMIC_SEQ_CST))
                        return;

                /* Wait for successor to appear */
                while (!me->next)
                        cpu_idle();
        }

        /* Unlock next one */
        me->next->spin = 1;
}

static inline int trylock_mcs(spin_lock *m, spin_lock_t *me)
{
        me->next = (spin_lock_t *)NULL;
        me->spin = 0;

        /* No one was there - can quickly return */
        if (!__atomic_compare_exchange_n(
                    m, NULL, me, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
                return 0;

        return 1; // Busy
}
#endif