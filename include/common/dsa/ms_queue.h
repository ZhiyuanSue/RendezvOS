#ifndef _RENDEZVOS_LOCK_FREE_LIST_H_
#define _RENDEZVOS_LOCK_FREE_LIST_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/taggedptr.h>
#include <common/refcount.h>
#include <rendezvos/error.h>
#include <rendezvos/task/ebr.h>

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
typedef struct {
        ref_count_t refcount;
        tagged_ptr_t next;
} ms_queue_node_t;

typedef struct Michael_Scott_Queue {
        tagged_ptr_t head;
        tagged_ptr_t tail;
        size_t append_info_bits;
} ms_queue_t;
/**
 * @brief Initialize an empty Michael–Scott queue with a dummy node.
 * @param q Queue structure.
 * @param new_node Pre-allocated empty node used as initial head/tail dummy
 *        (caller allocates; not freed by init).
 * @param append_info_bits Bits of the 16-bit tag reserved for append state
 *        (e.g. port SEND/RECV). Must ≤15 so one bit remains for ABA
 *        counter.
 */
static inline void msq_init(ms_queue_t* q, ms_queue_node_t* new_node,
                            size_t append_info_bits)
{
        if (!q || !new_node) {
                return;
        }
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
 * @brief Enqueue a @p new_node (Michael–Scott)
 * @param q Queue.
 * @param new_node Caller-allocated node with live refcount (≥1).
 * @param free_func Passed to @c ref_put when releasing.
 * @note Does not embed payload — use @c container_of .
 *  No append-tag check (see @c msq_enqueue_check_tail ).
 */
static inline void msq_enqueue(ms_queue_t* q, ms_queue_node_t* new_node,
                               error_t (*free_func)(ref_count_t*))
{
        if (!q || !new_node) {
                return;
        }
        ebr_enter();
        tagged_ptr_t tail, next, tmp;

        atomic64_store((volatile u64*)(&(new_node->next)), 0);
        while (1) {
                tail = q->tail;
                ms_queue_node_t* tail_node = (ms_queue_node_t*)tp_get_ptr(tail);
                if (!tail_node || !ref_get_not_zero(&tail_node->refcount))
                        continue;

                next = tail_node->next;

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
        ebr_exit();
}
/**
 * @brief Dequeue one logical node (Michael–Scott “lazy dequeue”).
 *
 * Advances head so the former data node becomes the new dummy and stays
 * linked
 * — the returned tagged ptr is that node with an extra ref for the
 * caller
 * - the old dummy is @c ref_put( @p free_func ).
 * - if queue is empty just return tp_new_none() and not put the dummy
 *
 * @param free_func May be NULL to skip freeing (pooled dummy).
 * @return Tagged data node, or none tagged ptr. Caller must @c ref_put when
 * done if not none.
 * @note Wrapped in @c ebr_enter / @c ebr_exit. Do not relocate the returned
 *       node into another MSQ.
 */
static inline tagged_ptr_t msq_dequeue(ms_queue_t* q,
                                       error_t (*free_func)(ref_count_t*))
{
        if (!q)
                return tp_new_none();
        ebr_enter();
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();

        while (1) {
                head = q->head;
                ms_queue_node_t* head_node = (ms_queue_node_t*)tp_get_ptr(head);

                if (!head_node || !ref_get_not_zero(&head_node->refcount))
                        continue;

                tail = q->tail;
                next = head_node->next;

                if (atomic64_cas(
                            (volatile u64*)&q->head, *(u64*)&head, *(u64*)&head)
                    == *(u64*)&head) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        ebr_exit();
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
        ebr_exit();
        return res;
}

/**
 * @brief Drain a queue; optionally clear head/tail after drain.
 *
 * Semantics of `msq_dequeue`:
 * - Each returned payload node must be `ref_put(..., free_func)` by the caller
 *   (handled in the while-loop below).
 * - When the queue becomes empty (only dummy, `next == NULL`), `msq_dequeue`
 *   already `ref_put`s the dummy node internally and returns `tp_new_none()`.
 *   Therefore **do not** `ref_put` the dummy again here — that would
 * double-free.
 *
 * After a full drain, `head`/`tail` may still hold stale tagged pointers; if
 * @p zero_head_tail is true, clear them to NULL for hygiene (no refcount).
 *
 * @param zero_head_tail If true, set `q->head` and `q->tail` to 0 after drain.
 * @param free_func Passed to `msq_dequeue` / per-node `ref_put` in the loop.
 */
static inline void msq_clean_queue(ms_queue_t* q, bool zero_head_tail,
                                   error_t (*free_func)(ref_count_t*))
{
        if (!q)
                return;
        tagged_ptr_t dequeued_ptr;
        while (!tp_is_none(dequeued_ptr = msq_dequeue(q, free_func))) {
                ref_put(&((ms_queue_node_t*)tp_get_ptr(dequeued_ptr))->refcount,
                        free_func);
        }
        if (zero_head_tail) {
                atomic64_store((volatile u64*)&q->head, 0);
                atomic64_store((volatile u64*)&q->tail, 0);
        }
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
 * @brief Enqueue only if current tail’s append-tag bits match @p expect_tp.
 *
 * On append check fail returns @c -E_REND_AGAIN (caller can retry).
 * If @c append_info_bits==0, just like normal @c msq_enqueue.
 *
 * @param append_info New node’s append field written into the tag low bits.
 * @param expect_tp Expected tail tag (append field compared under mask).
 */
static inline error_t msq_enqueue_check_tail(ms_queue_t* q,
                                             ms_queue_node_t* new_node,
                                             u64 append_info,
                                             tagged_ptr_t expect_tp,
                                             error_t (*free_func)(ref_count_t*))
{
        if (!q || !new_node) {
                return -E_IN_PARAM;
        }
        ebr_enter();
        tagged_ptr_t tail, next, tmp;
        if (q->append_info_bits == 0) {
                msq_enqueue(q, new_node, free_func);
                ebr_exit();
                return REND_SUCCESS;
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;

        atomic64_store((volatile u64*)(&(new_node->next)), 0);
        while (1) {
                tail = q->tail;
                ms_queue_node_t* tail_node = (ms_queue_node_t*)tp_get_ptr(tail);
                if (!tail_node || !ref_get_not_zero(&tail_node->refcount))
                        continue;

                next = tail_node->next;

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (!msq_queue_check_tp(tail,
                                                MSQ_CHECK_FIELD_APPEND,
                                                expect_tp,
                                                append_info_mask)) {
                                ref_put(&tail_node->refcount, free_func);
                                ebr_exit();
                                return -E_REND_AGAIN;
                        }
                        if (tp_get_ptr(next) == NULL) {
                                if (!ref_get_not_zero(&new_node->refcount)) {
                                        ref_put(&tail_node->refcount,
                                                free_func);
                                        ebr_exit();
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
        ebr_exit();
        return REND_SUCCESS;
}

/**
 * @brief Dequeue only if the next node’s tagged fields match @p expect_tp.
 *
 * compare ptr and/or append bits according to
 * @p check_field_mask ( @c MSQ_CHECK_FIELD_PTR / @c MSQ_CHECK_FIELD_APPEND ).
 * 
 * Mismatch → return none.
 * if @c append_info_bits==0, just as normal @c msq_dequeue.
 */
static inline tagged_ptr_t
msq_dequeue_check_head(ms_queue_t* q, u64 check_field_mask,
                       tagged_ptr_t expect_tp,
                       error_t (*free_func)(ref_count_t*))
{
        if (!q)
                return tp_new_none();
        ebr_enter();
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();
        if (q->append_info_bits == 0
            && ((check_field_mask & MSQ_CHECK_FIELD_PTR) == 0)) {
                /*no append info and no need to check the ptr ,normal case*/
                tagged_ptr_t out = msq_dequeue(q, free_func);
                ebr_exit();
                return out;
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;
        while (1) {
                head = q->head;
                ms_queue_node_t* head_node = (ms_queue_node_t*)tp_get_ptr(head);
                if (!head_node || !ref_get_not_zero(&head_node->refcount))
                        continue;
                next = head_node->next;
                tail = q->tail;
                if (atomic64_cas(
                            (volatile u64*)&q->head, *(u64*)&head, *(u64*)&head)
                    == *(u64*)&head) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        ref_put(&head_node->refcount,
                                                free_func);
                                        ebr_exit();
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
                                        ebr_exit();
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
        ebr_exit();
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