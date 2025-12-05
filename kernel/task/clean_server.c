#include <rendezvos/task/ipc_server.h>
#include <modules/log/log.h>

char clean_server_thread_name[] = "clean_server_thread";
void clean_server_thread(void)
{
        while (1) {
                schedule(percpu(core_tm));
        }
}
error_t create_clean_server_thread(void)
{
        Thread_Base* ipc_server_t;
        ipc_server_t = create_thread((void*)clean_server_thread, 0);
        if (!ipc_server_t) {
                pr_error("[Error] create test thread fail\n");
                return -E_RENDEZVOS;
        }
        thread_set_name(clean_server_thread_name, ipc_server_t);
        error_t e = thread_join(percpu(core_tm)->current_task,
                                ipc_server_t); /*current task must be root
                                                  task*/
        return e;
}