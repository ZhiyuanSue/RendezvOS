#include <rendezvos/task/thread_loader.h>
#include <modules/log/log.h>

char clean_server_thread_name[] = "clean_server_thread";
void clean_server_thread(void)
{
        while (1) {
                schedule(percpu(core_tm));
        }
}