#ifndef _RENDEZVOS_REFCOUNT_H_
#define _RENDEZVOS_REFCOUNT_H_

#include "types.h"
#include "atomic.h"
#include "stdbool.h"
#include "stddef.h"
typedef struct {
        atomic64_t counter;
} ref_count_t;
/**
 * @brief Initialize refcount to 1 (for dummy or single-owner node).
 * @param node call before putting in queue as the initial dummy.
 */
static inline void ref_init(ref_count_t* refcount)
{
        atomic64_init(&refcount->counter, 1);
}

/**
 * @brief Initialize refcount to 0 (for a node to be enqueued).
 *        Enqueue will ref_get before linking. Call for new nodes.
 */
static inline void ref_init_zero(ref_count_t* refcount)
{
        atomic64_init(&refcount->counter, 0);
}

/**
 * @brief Increment refcount only if it was > 0. Prevents "revival" of a dying
 *        object (refcount 0, another CPU about to free).
 * @return true if ref acquired, false if refcount was 0 (object is dying).
 */
static inline bool ref_get_not_zero(ref_count_t* refcount)
{
        i64 old;
        do {
                old = (i64)atomic64_load(
                        (volatile const u64*)&refcount->counter.counter);
                if (old <= 0)
                        return false;
        } while (atomic64_cas((volatile u64*)&refcount->counter.counter,
                              (u64)old,
                              (u64)(old + 1))
                 != (u64)old);
        return true;
}

/**
 * @brief Claim a node with refcount 0 (e.g. new node before enqueue).
 *        0 -> 1 via CAS. Use only for nodes you know start at 0.
 * @return true if claimed, false if refcount was not 0.
 */
static inline bool ref_get_claim(ref_count_t* refcount)
{
        return atomic64_cas((volatile u64*)&refcount->counter.counter, 0, 1)
               == 0;
}

/**
 * @brief Decrement refcount; if it becomes 0 and free_func is non-NULL,
 *        call free_func(node). Pass NULL to decrement without freeing.
 */
static inline void ref_put(ref_count_t* refcount,
                           void (*free_func)(ref_count_t*))
{
        i64 prev = atomic64_fetch_dec(&refcount->counter);
        if (prev == 1 && free_func != NULL) {
                free_func(refcount);
        }
}

/**
 * @brief Read current refcount (racy: only for debugging or heuristics).
 *        Do not use the result to decide whether the node is still valid.
 */
static inline i64 ref_count(ref_count_t* refcount)
{
        return (i64)atomic64_load(
                (volatile const u64*)&refcount->counter.counter);
}

#endif