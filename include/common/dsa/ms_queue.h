#ifndef _RENDEZVOS_LOCK_FREE_LIST_H_
#define _RENDEZVOS_LOCK_FREE_LIST_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/taggedptr.h>
#include <rendezvos/sync/atomic.h>
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
typedef struct {
        tagged_ptr_t next;
} ms_queue_node_t;

typedef struct {
        tagged_ptr_t head;
        tagged_ptr_t tail;
        size_t append_info_bits;
} ms_queue_t;
/**
 * @brief init the msq
 * @param q the queue structure
 * @param new_node use an empty node as the head, this must be allocated by the
 * caller function
 */
static inline void msq_init(ms_queue_t* q, ms_queue_node_t* new_node,
                            size_t append_info_bits)
{
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
 * @param append_info_bits, which define the append_info length
 * @param append_info,  which also store the append_info in the 16bits tag
 */
static inline void msq_enqueue(ms_queue_t* q, ms_queue_node_t* new_node,
                               u64 append_info)
{
        tagged_ptr_t tail, next, tmp;
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;

        new_node->next = 0;
        while (1) {
                tail = atomic64_load(&q->tail);
                next = ((ms_queue_node_t*)tp_get_ptr(tail))->next;

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (tp_get_ptr(next) == NULL) {
                                tmp = tp_new(new_node,
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (append_info
                                                        & append_info_mask));
                                if (atomic64_cas(
                                            (volatile u64*)&(
                                                    (ms_queue_node_t*)
                                                            tp_get_ptr(tail))
                                                    ->next,
                                            *(u64*)&next,
                                            *(u64*)&tmp)
                                    == *(u64*)&next) {
                                        break;
                                }
                        } else {
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(tail)
                                                        & append_info_mask));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        }
                }
        }
}
/**
 * @brief dequeue a node and return the ptr
 * @param q, the ms queue
 * @param append_info_bits, which define the append_info length
 * @return , we return the old head node, the real valid head node is return
 * node's next
 * @note we also just unlink the node,
 * but don't free the node,
 * which should be done by the upper level code
 * beside, the data node must be the return ptr->next,
 * see test function smp_ms_queue_dyn_alloc_get
 * the append info is no need to set again
 */
static inline tagged_ptr_t msq_dequeue(ms_queue_t* q)
{
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;
        while (1) {
                head = atomic64_load(&q->head);
                tail = atomic64_load(&q->tail);
                next = atomic64_load(
                        &(((ms_queue_node_t*)tp_get_ptr(head))->next));
                if (atomic64_cas((volatile u64*)&q->head,
                                 *(u64*)&head,
                                 *(u64*)&head)) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        /*
                                                if the queue is empty ,
                                                let the idle node's append info
                                           be 0, if you want to use the append
                                           info represent something, 0 must be
                                           the normal case,
                                        */
                                        tmp = tp_new(
                                                tp_get_ptr(tail),
                                                ((tp_get_tag(tail) + tag_step)
                                                 & tag_mask));
                                        /*
                                                only need try once,
                                                if succ,the queue be empty,
                                                if fail,new node is enqueue,
                                                and no more need for this idle
                                           node's info
                                        */
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        return tp_new_none();
                                }
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        } else {
                                res = head;
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                if (atomic64_cas((volatile u64*)&q->head,
                                                 *(u64*)&head,
                                                 *(u64*)&tmp)
                                    == *(u64*)&head) {
                                        break;
                                }
                        }
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
#define MSA_CHECK_FIELD_PTR    1
#define MSQ_CHECK_FIELD_APPEND 2
static inline bool msq_queue_check_tp(tagged_ptr_t need_check_tp,
                                      u64 check_field_mask,
                                      tagged_ptr_t expect_tp,
                                      u64 append_info_bits)
{
        if (check_field_mask & MSA_CHECK_FIELD_PTR) {
                if (tp_get_ptr(need_check_tp) != tp_get_ptr(expect_tp)) {
                        return false;
                }
        }
        u16 append_info_mask = (1 << append_info_bits) - 1;
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
 * @param append_info_bits, which define the append_info length
 * @param append_info,  which also store the append_info in the 16bits tag
 * @param expect_append_info, the expected match info
 * @return , we return whether have we successfully enqueue, REND_SUCCESS -
 * success, other - fail
 */
static inline error_t msq_enqueue_check_tail(ms_queue_t* q,
                                             ms_queue_node_t* new_node,
                                             u64 append_info,
                                             tagged_ptr_t expect_tp)
{
        tagged_ptr_t tail, next, tmp;
        if (q->append_info_bits == 0) {
                msq_enqueue(q, new_node, append_info);
                return REND_SUCCESS;
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;

        new_node->next = 0;
        while (1) {
                tail = atomic64_load(&q->tail);
                next = ((ms_queue_node_t*)tp_get_ptr(tail))->next;

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (!msq_queue_check_tp(tail,
                                                MSQ_CHECK_FIELD_APPEND,
                                                expect_tp,
                                                q->append_info_bits)) {
                                return -E_REND_AGAIN;
                        }
                        if (tp_get_ptr(next) == NULL) {
                                tmp = tp_new(new_node,
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (append_info
                                                        & append_info_mask));
                                if (atomic64_cas(
                                            (volatile u64*)&(
                                                    (ms_queue_node_t*)
                                                            tp_get_ptr(tail))
                                                    ->next,
                                            *(u64*)&next,
                                            *(u64*)&tmp)
                                    == *(u64*)&next) {
                                        break;
                                }
                        } else {
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(tail)
                                                        & append_info_mask));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        }
                }
        }
        return REND_SUCCESS;
}

/**
 * @brief dequeue a node and check the head node's ptr and/or append
 * info(pointed by check_field_mask), if match, return the ptr
 * @param q, the ms queue
 * @param check_field_mask, a mask that decide which file to be
 * checked(MSA_CHECK_FIELD_PTR/MSQ_CHECK_FIELD_APPEND)
 * @param expect_append_info, the expected match info
 * @return , we return the old head node, the real valid head node is return
 * node's next
 * @note others are all the same with the msq_dequeue
 */
static inline tagged_ptr_t msq_dequeue_check_head(ms_queue_t* q,
                                                  u64 check_field_mask,
                                                  tagged_ptr_t expect_tp)
{
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();
        if (q->append_info_bits == 0
            && ((check_field_mask & MSA_CHECK_FIELD_PTR) == 0)) {
                /*no append info and no need to check the ptr ,normal case*/
                return msq_dequeue(q);
        }
        u16 tag_step = 1 << q->append_info_bits;
        u16 append_info_mask = tag_step - 1;
        u16 tag_mask = ~append_info_mask;
        while (1) {
                head = atomic64_load(&q->head);
                tail = atomic64_load(&q->tail);
                next = atomic64_load(
                        &(((ms_queue_node_t*)tp_get_ptr(head))->next));
                if (atomic64_cas((volatile u64*)&q->head,
                                 *(u64*)&head,
                                 *(u64*)&head)) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        /*
                                                the same with the msq_dequeue
                                        */
                                        tmp = tp_new(
                                                tp_get_ptr(tail),
                                                ((tp_get_tag(tail) + tag_step)
                                                 & tag_mask));
                                        atomic64_cas((volatile u64*)&q->tail,
                                                     *(u64*)&tail,
                                                     *(u64*)&tmp);
                                        return tp_new_none();
                                }
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        } else {
                                if (!msq_queue_check_tp(next,
                                                        check_field_mask,
                                                        expect_tp,
                                                        q->append_info_bits)) {
                                        return tp_new_none();
                                }
                                res = head;
                                tmp = tp_new(tp_get_ptr(next),
                                             ((tp_get_tag(tail) + tag_step)
                                              & tag_mask)
                                                     | (tp_get_tag(next)
                                                        & append_info_mask));
                                if (atomic64_cas((volatile u64*)&q->head,
                                                 *(u64*)&head,
                                                 *(u64*)&tmp)
                                    == *(u64*)&head) {
                                        break;
                                }
                        }
                }
        }
        return res;
}
#endif