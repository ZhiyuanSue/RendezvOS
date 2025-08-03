#ifndef _RENDEZVOS_LOCK_FREE_LIST_H_
#define _RENDEZVOS_LOCK_FREE_LIST_H_

#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <rendezvos/sync/atomic.h>

/*a lock free implement*/

/*
we should at least include 4 ops:
 - init a empty linked list
 - enqueue
 - get the snapshot
 - dequeue
 - others(for each ?)
the writer enqueue the list
the reader get the snapshot and use for each to dequeue
*/

#endif