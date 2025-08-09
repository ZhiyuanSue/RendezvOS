#ifndef _RENDEZVOS_LOCK_FREE_LIST_H_
#define _RENDEZVOS_LOCK_FREE_LIST_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/taggedptr.h>
#include <rendezvos/sync/atomic.h>

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
} ms_queue_t;

static inline void msq_init(ms_queue_t* q, ms_queue_node_t* new_node)
{
        q->head = q->tail = tp_new((void*)new_node, 0);
}

static inline void msq_enqueue(ms_queue_t* q, ms_queue_node_t* new_node)
{
        tagged_ptr_t tail, next, tmp;

        tp_set_ptr(&new_node->next, NULL);
        while (1) {
                tail = q->tail;
                next = ((ms_queue_node_t*)tp_get_ptr(tail))->next;

                if (atomic64_cas(
                            (volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tail)
                    == *(u64*)&tail) {
                        if (tp_get_ptr(next) == NULL) {
                                tmp = tp_new(new_node, tp_get_tag(next) + 1);
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
                                             tp_get_tag(tail) + 1);
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        }
                }
        }
        tmp = tp_new(new_node, tp_get_tag(tail) + 1);
        atomic64_cas((volatile u64*)&q->tail, *(u64*)&tail, *(u64*)&tmp);
}
/*we also just unlink the node,
        but don't free the node,
        which should be done by the upper level code*/
static inline tagged_ptr_t msq_dequeue(ms_queue_t* q)
{
        tagged_ptr_t head, tail, next, tmp;
        tagged_ptr_t res = tp_new_none();
        while (1) {
                head = q->head;
                tail = q->tail;
                next = ((ms_queue_node_t*)tp_get_ptr(head))->next;
                if (atomic64_cas((volatile u64*)&q->head,
                                 *(u64*)&head,
                                 *(u64*)&head)) {
                        if (tp_get_ptr(head) == tp_get_ptr(tail)) {
                                if (tp_get_ptr(next) == NULL) {
                                        return tp_new_none();
                                }
                                tmp = tp_new(tp_get_ptr(next),
                                             tp_get_tag(tail) + 1);
                                atomic64_cas((volatile u64*)&q->tail,
                                             *(u64*)&tail,
                                             *(u64*)&tmp);
                        } else {
                                res = next;
                                tmp = tp_new(tp_get_ptr(next),
                                             tp_get_tag(head) + 1);
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