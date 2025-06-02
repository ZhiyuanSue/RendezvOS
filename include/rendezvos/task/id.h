#ifndef _RENDEZVOS_ID_H_
#define _RENDEZVOS_ID_H_

#include <common/types.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>
#define INVALID_ID -1
/*
as for the tid and pid , which must be global
we cannot expect two thread or task have same tid or pid
however, multicore system might need to lock it
*/
typedef struct {
        i64 id;
        spin_lock spin_ptr;
} Id_Manager;
void init_id_manager(Id_Manager* im);
i64 get_new_pid();
i64 get_new_tid();

#endif