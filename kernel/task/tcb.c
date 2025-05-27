#include <rendezvos/task/tcb.h>
#include <rendezvos/percpu.h>
DEFINE_PER_CPU(Tcb_Base*, current_task);

Tcb_Base* init_proc()
{
        return NULL;
}
void schedule()
{
}