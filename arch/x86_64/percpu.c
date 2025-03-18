#include <rendezvos/percpu.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/msr.h>
vaddr get_per_cpu_base()
{
        vaddr addr = rdmsr(MSR_GS_BASE);
        return addr;
}