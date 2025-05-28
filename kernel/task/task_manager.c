#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
DEFINE_PER_CPU(Task_Manager*, core_tm);
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        return NULL;
}