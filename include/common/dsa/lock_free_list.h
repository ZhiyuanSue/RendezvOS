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

#endif