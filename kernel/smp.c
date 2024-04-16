#include <shampoos/smp.h>
#include <modules/log/log.h>
void start_smp()
{
#ifndef SMP
	return;
#endif
	pr_info("start smp\n");
	
}