#include <modules/log/log.h>
#include <shampoos/smp.h>
void start_smp(void)
{
#ifndef SMP
        return;
#endif

        pr_info("start smp\n");
        arch_start_smp();
}