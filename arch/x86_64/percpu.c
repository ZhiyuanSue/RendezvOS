#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/msr.h>
#include <rendezvos/smp/percpu.h>
vaddr get_per_cpu_base(void)
{
        vaddr addr = rdmsrq(MSR_GS_BASE);
        return addr;
}