#include <modules/log/log.h>
#include <modules/test/test.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/common.h>
int NR_CPU = 1;
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
        print("[ CPU%d ]", current_cpu_id);
        hello_world();
#endif
        if (virt_mm_init(current_cpu_id, arch_setup_info)) {
                pr_error("[ERROR] virt mm init error\n");
                return;
        }
        if (start_arch(current_cpu_id)) {
                pr_error("[ERROR] start cpu arch fail\n");
                return;
        }
        pr_info("successfully start secondary cpu %d\n", current_cpu_id);
        per_cpu(CPU_STATE, current_cpu_id) = cpu_enable;

        AP_test();

        cpu_idle();
}