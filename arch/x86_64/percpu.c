#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/msr.h>
#include <rendezvos/smp/percpu.h>
void arch_enable_percpu(cpu_id_t cpu_id)
{
        /*
         gs
         as it's per_cpu base
         only after we set the gs base can we use percpu
        */
        wrmsrq(MSR_GS_BASE, __per_cpu_offset[cpu_id]);
}
vaddr get_per_cpu_base(void)
{
        vaddr addr = rdmsrq(MSR_GS_BASE);
        return addr;
}
