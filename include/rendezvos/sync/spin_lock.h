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
/**
 * @brief Per-waiter MCS queue node (must be CPU-private for the holder).
 */
struct spin_lock_t {
        spin_lock_t *next;
        u64 spin;
};
/**
 * @brief Queue head pointer (NULL = unlocked).
 */
typedef struct spin_lock_t *spin_lock;

/**
 * @brief Acquire an MCS lock.
 *
 * @param m  Address of the lock head pointer
 * @param me This CPU's waiter node (per-CPU / holder-private).
 *
 * Waiting path spins with @c atomic64_load on @c me->spin (pairs with
 * unlock's @c atomic64_store handoff).
 *
 * [中文临时对照 — 审阅后可删]
 * @brief 获取一把 MCS 锁。
 * @param m  锁头指针的地址
 * @param me 本 CPU 的 waiter 节点（per-CPU / 持有者私有）。
 * 等待路径对 @c me->spin 用 @c atomic64_load 自旋（与 unlock 的
 * @c atomic64_store 交棒配对）。
 */
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
        atomic64_store((volatile u64 *)&tail->next, (u64)me);

        /* Spin on my spin variable (acquire vs unlock store). */
        while (!atomic64_load((volatile u64 *)&me->spin))
                arch_cpu_relax();

        return;
}

/**
 * @brief Release an MCS lock held with @p me.
 */
static inline void unlock_mcs(spin_lock *m, spin_lock_t *me)
{
        /* No successor yet? */
        if (!me->next) {
                /* Try to atomically unlock */
                if (atomic64_cas((volatile u64 *)m, (u64)me, (u64)NULL)
                    == (u64)me)
                        return;

                /* Wait for successor to appear */
                while (!atomic64_load((volatile u64 *)&me->next))
                        arch_cpu_relax();
        }

        /* Unlock next one */
        atomic64_store((volatile u64 *)&me->next->spin, 1);
}

/**
 * @brief Non-blocking MCS try: acquire only if the queue is empty.
 *
 * @return 0 if acquired, 1 if busy
 */
static inline int trylock_mcs(spin_lock *m, spin_lock_t *me)
{
        spin_lock_t *tail;

        me->next = (spin_lock_t *)NULL;
        me->spin = 0;

        /* Try to lock */
        tail = (spin_lock_t *)atomic64_cas(
                (volatile u64 *)m, (u64)NULL, (u64)me);
        /* No one was there - can quickly return */
        if (!tail)
                return 0;

        return 1; // Busy
}
#endif