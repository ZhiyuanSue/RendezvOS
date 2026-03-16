#ifndef _RENDEZVOS_ID_H_
#define _RENDEZVOS_ID_H_

#include <common/types.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>
#include <common/limits.h>
#define INVALID_ID -1
/*
as for the tid and pid , which must be global
we cannot expect two thread or task have same tid or pid
however, multicore system might need to lock it
*/
typedef struct {
        i64 id;
        spin_lock spin_ptr;
        spin_lock_t* cpu_spin_lock;
} Id_Manager;
void init_id_manager(Id_Manager* idmng,spin_lock_t* cpu_spin_lock);
i64 get_new_id(Id_Manager* idmng);
void init_id_managers();

extern spin_lock_t pid_spin_lock;
extern spin_lock_t tid_spin_lock;

extern Id_Manager pid_manager;
extern Id_Manager tid_manager;

#endif