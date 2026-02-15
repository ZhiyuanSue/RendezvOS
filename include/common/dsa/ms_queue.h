#ifndef _RENDEZVOS_LOCK_FREE_LIST_H_
#define _RENDEZVOS_LOCK_FREE_LIST_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/taggedptr.h>
#include <common/refcount.h>
#include <rendezvos/error.h>

/*a lock free implement*/

/*
we should at least include 4 ops:
 - init a empty linked list
 - enqueue
 - get the snapshot -> delete
 - dequeue
 - others(for each ?) -> delete
the writer enqueue the list
the reader get the snapshot and use for each to dequeue

see Michael and Scott
Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue
Algorithms

besides,
in order to realise a independent header file
we do not alloc the new node here
and we do not include the data, please use the container_of

in order to avoid the ABA problem, we need to add the
*/

/* Reference count: nodes in the queue have refcount >= 1. Dequeue returns
 * the data node (head->next) with refcount already incremented; caller
 * must msq_node_ref_put when done. Old dummy is ref_put with free_func. */
typedef struct Michael_Scott_Queue ms_queue_t;
typedef struct {
        ref_count_t refcount;
        tagged_ptr_t next;
        ms_queue_t* queue_ptr;
} ms_queue_node_t;

struct Michael_Scott_Queue {
        tagged_ptr_t head;
        tagged_ptr_t tail;
        size_t append_info_bits;
};
/**
 * @brief init the msq
 * @param q the queue structure
 * @param new_node use an empty node as the head, this must be allocated by the
 * caller function
 */
static inline void msq_init(ms_queue_t* q, ms_queue_node_t* new_node,
                            size_t append_info_bits)
{
        ref_init(&new_node->refcount);
        new_node->next = tp_new_none();
        q->head = q->tail = tp_new((void*)new_node, 0);
        q->append_info_bits = append_info_bits;
        if (q->append_info_bits >= 16) {
                /*we must left 1 bit for tag*/
                q->append_info_bits = 15;
        }
}
/**
 * @brief enqueue a new_node into the queue
 * @param q the queue structure
 * @param new_node, which should be allocated by the caller function
 * @param append_info, which also store the append_info in the 16bits tag
 * @param free_func called when old tail's ref drops to 0
 * @param refcount_is_zero true: node has refcount 0 (ref_init_zero), use
 * ref_get_claim; false: node has refcount >= 1, use ref_get_not_zero (avoids
 * revival)
 */
static inline void msq_enqueue(ms_queue_t* q, ms_queue_node_t* new_node,
                               void (*free_func)(ref_count_t*))
{
        if (!q || new_node->queue_ptr) {
                return;
        }
        tagged_ptr_t tail, next, tmp;

        atomic64_store((volatile u64*)(&(new_node->next)), 0);
        while (1) {
                tail = atomic64_load(&q->tail);
                ms_queue_node_t* tail_node = (ms_queue_node_t*)tp_get_ptr(tail);
                if (!tail_node || !ref_get_not_zero(&tail_node->refcount))
                        continue;

                next = atomic64_load((volatile u64*)(&(tail_node->next)));

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (tp_get_ptr(next) == NULL) {
                                if (!ref_get_not_zero(&new_node->refcount)) {
                                        ref_put(&tail_node->refcount,
                                                free_func);
                                        continue;
                                }
                                tmp = tp_new(new_node, (tp_get_tag(tail) + 1));
                                if (atomic64_cas((volatile u64*)&tail_node->next,
                                                 *(u64*)&next,
                                                 *(u64*)&tmp)
                                    == *(u64*)&next) {
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        atomic64_store((volatile u64*)&new_node
                                                               ->queue_ptr,
                                                       (u64)q);
                                        ref_put(&tail_node->refcount,
                                                free_func);
                                        break;
                                }
                                ref_put(&new_node->refcount, NULL);
                                ref_put(&tail_node->refcount, free_func);
                        } else {
                                tmp = tp_new(tp_get_ptr(next),
                                             (tp_get_tag(tail) + 1));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                                ref_put(&tail_node->refcount, free_func);
                        }
                } else {
                        ref_put(&tail_node->refcount, free_func);
                }
        }
}
/**
 * @brief dequeue a node and return the ptr
 * @param q the ms queue
 * @param free_func called when old dummy's ref drops to 0; may be NULL to
 *        not free (e.g. if dummy is pooled).
 * @return tagged_ptr of the data node (real payload), or tp_new_none() if
 *        queue empty. Caller must ref_put(ptr, free_func) when done.
 */
static inline tagged_ptr_t msq_dequeue(ms_queue_t* q,
                                       void (*free_func)(ref_count_t*))
{
        if (!q)
                return tp_new_none();
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();

        while (1) {
                head = atomic64_load(&q->head);
                ms_queue_node_t* head_node = (ms_queue_node_t*)tp_get_ptr(head);

                if (!head_node || !ref_get_not_zero(&head_node->refcount))
                        continue;

                tail = atomic64_load(&q->tail);
                next = atomic64_load(&head_node->next);

                if (atomic64_cas(
                            (volatile u64*)&q->head, *(u64*)&head, *(u64*)&head)
                    == *(u64*)&head) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        return tp_new_none();
                                } else {
                                        tmp = tp_new(tp_get_ptr(next),
                                                     (tp_get_tag(tail) + 1));
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        ref_put(&head_node->refcount,
                                                free_func);
                                }
                        } else {
                                ms_queue_node_t* next_node =
                                        (ms_queue_node_t*)tp_get_ptr(next);
                                if (!next_node) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        continue;
                                }

                                if (!ref_get_not_zero(&next_node->refcount)) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        continue;
                                }

                                /* Use tail tag for queue state (e.g. send/recv
                                 * in append_info). */
                                tmp = tp_new(next_node, (tp_get_tag(tail) + 1));
                                if (atomic64_cas((volatile u64*)&q->head,
                                                 *(u64*)&head,
                                                 *(u64*)&tmp)
                                    == *(u64*)&head) {
                                        /* Release our ref from
                                         * ref_get(head_node). */
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        /* Release queue's ref (head no longer
                                         * holds old dummy). Only here have
                                         * chance to free the dummy node*/
                                        atomic64_store((volatile u64*)&head_node
                                                               ->queue_ptr,
                                                       (u64)NULL);
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        res = next;
                                        break;
                                }
                                ref_put(&next_node->refcount, free_func);
                                ref_put(&head_node->refcount, free_func);
                        }
                } else {
                        ref_put(&head_node->refcount, free_func);
                }
        }
        return res;
}
/**
 * @brief this function is used for msq_dequeue_check_head and
 * msq_enqueue_check_tail. Only the checked tp is our expected tp, can we
 * return.
 * @param need_check_tp, the check tagged ptr need to check
 * @param check_field_mask, point out which field to be check.
 * @param expect_tp, the expected value of tagged ptr
 * @param append_info_bits, point out how much bits the append info used
 * @return true, pass the check .false, check fail.
 */
#define MSQ_CHECK_FIELD_PTR    1
#define MSQ_CHECK_FIELD_APPEND 2
static inline bool msq_queue_check_tp(tagged_ptr_t need_check_tp,
                                      u64 check_field_mask,
                                      tagged_ptr_t expect_tp,
                                      u16 append_info_mask)
{
        if (check_field_mask & MSQ_CHECK_FIELD_PTR) {
                if (tp_get_ptr(need_check_tp) != tp_get_ptr(expect_tp)) {
                        return false;
                }
        }
        if (check_field_mask & MSQ_CHECK_FIELD_APPEND) {
                if ((tp_get_tag(need_check_tp) & append_info_mask)
                    != (tp_get_tag(expect_tp) & append_info_mask)) {
                        return false;
                }
        }
        return true;
}
/**
 * @brief enqueue a new_node into the queue,
 * only if the tail node's append info is the same as expected
 * @param q the queue structure
 * @param new_node, which should be allocated by the caller function
 * @param append_info, which also store the append_info in the 16bits tag
 * @param expect_tp expected tail tag for the check
 * @param free_func called when old tail's ref drops to 0
 * @param refcount_is_zero same as msq_enqueue
 * @return REND_SUCCESS on success, -E_REND_AGAIN on check fail or ref acquire
 * fail
 */
static inline error_t msq_enqueue_check_tail(ms_queue_t* q,
                                             ms_queue_node_t* new_node,
                                             u64 append_info,
                                             tagged_ptr_t expect_tp,
                                             void (*free_func)(ref_count_t*))
{
        if (!q || !new_node) {
                return -E_IN_PARAM;
        }
        if (atomic64_load((volatile u64*)&(new_node->queue_ptr))) {
                return -E_REND_AGAIN;
        }
        tagged_ptr_t tail, next, tmp;
        if (q->append_info_bits == 0) {
                msq_enqueue(q, new_node, free_func);
                return REND_SUCCESS;
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;

        atomic64_store((volatile u64*)(&(new_node->next)), 0);
        while (1) {
                tail = atomic64_load(&q->tail);
                ms_queue_node_t* tail_node = (ms_queue_node_t*)tp_get_ptr(tail);
                if (!tail_node || !ref_get_not_zero(&tail_node->refcount))
                        continue;

                next = atomic64_load((volatile u64*)(&(tail_node->next)));

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (!msq_queue_check_tp(tail,
                                                MSQ_CHECK_FIELD_APPEND,
                                                expect_tp,
                                                append_info_mask)) {
                                ref_put(&tail_node->refcount, free_func);
                                return -E_REND_AGAIN;
                        }
                        if (tp_get_ptr(next) == NULL) {
                                if (!ref_get_not_zero(&new_node->refcount)) {
                                        ref_put(&tail_node->refcount,
                                                free_func);
                                        return -E_REND_AGAIN;
                                }
                                tmp = tp_new(new_node,
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (append_info
                                                        & append_info_mask));
                                if (atomic64_cas((volatile u64*)&tail_node->next,
                                                 *(u64*)&next,
                                                 *(u64*)&tmp)
                                    == *(u64*)&next) {
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        atomic64_store((volatile u64*)&new_node
                                                               ->queue_ptr,
                                                       (u64)q);
                                        ref_put(&tail_node->refcount,
                                                free_func);
                                        break;
                                }
                                ref_put(&new_node->refcount, NULL);
                                ref_put(&tail_node->refcount, free_func);
                        } else {
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                                ref_put(&tail_node->refcount, free_func);
                        }
                } else {
                        ref_put(&tail_node->refcount, free_func);
                }
        }
        return REND_SUCCESS;
}

/**
 * @brief Dequeue with check: return data node only if next matches expect_tp.
 * @param q the ms queue
 * @param check_field_mask MSQ_CHECK_FIELD_PTR and/or MSQ_CHECK_FIELD_APPEND
 * @param expect_tp expected value for the check
 * @param free_func same as msq_dequeue
 * @return tagged_ptr of data node (ref held) or tp_new_none() if empty or
 *         check failed. Caller must ref_put when done.
 */
static inline tagged_ptr_t
msq_dequeue_check_head(ms_queue_t* q, u64 check_field_mask,
                       tagged_ptr_t expect_tp, void (*free_func)(ref_count_t*))
{
        if (!q)
                return tp_new_none();
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();
        if (q->append_info_bits == 0
            && ((check_field_mask & MSQ_CHECK_FIELD_PTR) == 0)) {
                /*no append info and no need to check the ptr ,normal case*/
                return msq_dequeue(q, free_func);
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;
        while (1) {
                head = atomic64_load(&q->head);
                ms_queue_node_t* head_node = (ms_queue_node_t*)tp_get_ptr(head);
                if (!head_node || !ref_get_not_zero(&head_node->refcount))
                        continue;
                tail = atomic64_load(&q->tail);
                next = atomic64_load(&head_node->next);
                if (atomic64_cas(
                            (volatile u64*)&q->head, *(u64*)&head, *(u64*)&head)
                    == *(u64*)&head) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        return tp_new_none();
                                } else {
                                        tmp = tp_new(
                                                tp_get_ptr(next),
                                                ((tp_get_tag(tail) + tag_step)
                                                 & tag_mask)
                                                        | (tp_get_tag(next)
                                                           & append_info_mask));
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        ref_put(&head_node->refcount,
                                                free_func);
                                }
                        } else {
                                if (!msq_queue_check_tp(next,
                                                        check_field_mask,
                                                        expect_tp,
                                                        append_info_mask)) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        return tp_new_none();
                                }
                                ms_queue_node_t* next_node =
                                        (ms_queue_node_t*)tp_get_ptr(next);
                                if (!next_node) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        continue;
                                }
                                if (!ref_get_not_zero(&next_node->refcount)) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        continue;
                                }
                                /* Use tail tag for queue state (e.g. send/recv
                                 * in append_info).
                                 */
                                tmp = tp_new(next_node,
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                if (atomic64_cas((volatile u64*)&q->head,
                                                 *(u64*)&head,
                                                 *(u64*)&tmp)
                                    == *(u64*)&head) {
                                        /* Release our ref from
                                         * ref_get(head_node). */
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        /* Release queue's ref (head no longer
                                         * holds old dummy). */
                                        atomic64_store((volatile u64*)&head_node
                                                               ->queue_ptr,
                                                       (u64)NULL);
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        if (tp_get_ptr(next)
                                            == tp_get_ptr(tail)) {
                                                tmp = tp_new(tp_get_ptr(tail),
                                                             ((tp_get_tag(tail)
                                                               + tag_step)
                                                              & tag_mask));

                                                atomic64_cas(
                                                        (volatile u64*)&q->tail,
                                                        *(u64*)&tail,
                                                        *(u64*)&tmp);
                                        }
                                        res = next;
                                        break;
                                }
                                ref_put(&next_node->refcount, free_func);
                                ref_put(&head_node->refcount, free_func);
                        }
                } else {
                        ref_put(&head_node->refcount, free_func);
                }
        }
        return res;
}

/*
 * Refcount and races after dequeue
 * --------------------------------
 * After msq_dequeue / msq_dequeue_check_head you hold one reference on the
 * returned data node. You may call msq_node_ref_count(node) to read the
 * current refcount, but that value is racy:
 *
 * 1. The value can change immediately: another thread may ref_get or ref_put
 *    after you read, so you cannot use it to decide "am I the only holder?"
 *    or "is it safe to free?".
 *
 * 2. The node must not be dereferenced after you call ref_put and
 *    the refcount drops to 0 (and free_func runs). So "read refcount then
 *    use node" is safe only while you still hold at least one ref (e.g. you
 *    have not yet called ref_put). After ref_put, the node may be freed.
 *
 * 3. The only safe use of refcount is: you know you did one dequeue (so you
 *    hold 1 ref). When you call ref_put, that ref is released; if the value
 *    was 1, ref_put may call free_func. So "get refcount for debugging or
 *    heuristics" is OK; "get refcount to decide whether to free" is wrong
 *    (use ref_put with the right free_func instead).
 *
 * Summary: you can read ref_count(refcount_ptr) after dequeue for debugging
 * or statistics, but do not rely on it for correctness. Correctness comes
 * from always pairing ref_get (from dequeue) with ref_put when done.
 */
#endif