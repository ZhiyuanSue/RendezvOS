#include <arch/aarch64/sys_ctrl.h>
#include <common/types.h>
vaddr get_per_cpu_base()
{
        vaddr addr;
        mrs("TPIDR_EL1", addr);
        return addr;
}