#include <arch/aarch64/sys_ctrl.h>
#include <common/types.h>
vaddr get_per_cpu_base(void)
{
        vaddr addr;
        mrs("TPIDR_EL1", addr);
        return addr;
}