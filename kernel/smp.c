#include <modules/log/log.h>
#include <shampoos/smp.h>
#include <shampoos/percpu.h>
int NR_CPU;
DEFINE_PER_CPU(enum cpu_status, CPU_STATE);
void start_smp(void)
{
#ifndef SMP
        return;
#endif
        pr_info("start smp\n");
        arch_start_smp();
}