#include <rendezvos/task/thread_loader.h>
#include <modules/log/log.h>

Id_Manager port_id_manager;
DEFINE_PER_CPU(struct spin_lock_t, port_id_spin_lock);

char ipc_server_thread_name[] = "ipc_server_thread";
void ipc_server_thread(void)
{
        while (1) {
                schedule(percpu(core_tm));
        }
}