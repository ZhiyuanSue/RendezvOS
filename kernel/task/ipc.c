#include <rendezvos/task/ipc.h>

Id_Manager port_id_manager;
DEFINE_PER_CPU(struct spin_lock_t, port_id_spin_lock);

char test_thread_name[] = "ipc_server_thread";
void ipc_server_thread(void)
{
        while (1) {
                schedule(percpu(core_tm));
        }
}
error_t create_ipc_server_thread()
{
        Thread_Base* ipc_server_t;
        ipc_server_t = create_thread((void*)ipc_server_thread, 0);
        if (!ipc_server_t) {
                pr_error("[Error] create test thread fail\n");
                return -E_RENDEZVOS;
        }
        thread_set_name(test_thread_name, ipc_server_t);
        error_t e = thread_join(percpu(core_tm)->current_task,
                                ipc_server_t); /*current task must be root task*/
        return e;
}