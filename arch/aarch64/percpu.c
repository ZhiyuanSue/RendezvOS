#include <arch/aarch64/sys_ctrl.h>
#include <common/types.h>
#include <rendezvos/smp/percpu.h>
void arch_enable_percpu(cpu_id_t cpu_id)
{
        msr("TPIDR_EL1", __per_cpu_offset[cpu_id]);
}
vaddr get_per_cpu_base(void)
{
        vaddr addr;
        mrs("TPIDR_EL1", addr);
        return addr;
}
