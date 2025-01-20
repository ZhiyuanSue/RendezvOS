#include <modules/log/log.h>
#include <shampoos/smp.h>
#include <shampoos/percpu.h>
#include <shampoos/common.h>
int NR_CPU;
DEFINE_PER_CPU(enum cpu_status, CPU_STATE);
void start_smp(struct setup_info *arch_setup_info)
{
#ifndef SMP
        return;
#endif
        pr_info("start smp\n");
        arch_start_smp(arch_setup_info);
}

void start_secondary_cpu(struct setup_info *arch_setup_info)
{
        int current_cpu_id = arch_setup_info->cpu_id;
#ifdef HELLO
        pr_info("[ CPU%d ]", current_cpu_id);
        hello_world();
#endif
        per_cpu(CPU_STATE, current_cpu_id) = cpu_enable;
        cpu_idle();
}