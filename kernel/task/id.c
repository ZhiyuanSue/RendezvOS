#include <rendezvos/task/id.h>
Id_Manager pid_manager;
Id_Manager tid_manager;
DEFINE_PER_CPU(struct spin_lock_t, pid_spin_lock);
DEFINE_PER_CPU(struct spin_lock_t, tid_spin_lock);

void init_id_manager(Id_Manager* idmng, spin_lock_t* cpu_spin_lock)
{
        if (!idmng) {
                return;
        }
        idmng->id = 0;
        idmng->spin_ptr = NULL;
        idmng->cpu_spin_lock = cpu_spin_lock;
}
i64 get_new_id(Id_Manager* idmng)
{
        if (!idmng) {
                return INVALID_ID;
        }
        lock_mcs(&idmng->spin_ptr, &percpu(*(idmng->cpu_spin_lock)));
        i64 id = idmng->id++;
        unlock_mcs(&idmng->spin_ptr, &percpu(*(idmng->cpu_spin_lock)));
        return id;
}
void init_id_managers()
{
        init_id_manager(&pid_manager, &percpu(pid_spin_lock));
        init_id_manager(&tid_manager, &percpu(tid_spin_lock));
}