#include <rendezvos/task/id.h>
Id_Manager pid_manager;
Id_Manager tid_manager;
DEFINE_PER_CPU(struct spin_lock_t, id_spin_lock);

void init_id_manager(Id_Manager* idmng)
{
        if (!idmng) {
                return;
        }
        idmng->id = 0;
        idmng->spin_ptr = NULL;
}
id_t get_new_id(Id_Manager* idmng)
{
        if (!idmng) {
                return INVALID_ID;
        }
        lock_mcs(&idmng->spin_ptr, &percpu(id_spin_lock));
        id_t id = idmng->id;
        if (id != INVALID_ID)
                idmng->id = id + 1;
        unlock_mcs(&idmng->spin_ptr, &percpu(id_spin_lock));
        return id;
}
void init_id_managers()
{
        init_id_manager(&pid_manager);
        init_id_manager(&tid_manager);
}
