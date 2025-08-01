#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/vmm.h>

DEFINE_PER_CPU(int, cpu_number);

u64 __per_cpu_offset[RENDEZVOS_MAX_CPU_NUMBER];

/*
the phy position of per cpu data is placed between kernel end and the pmm data
we only use RENDEZVOS_MAX_CPU_NUMBER-1
because the index 0 is using the space allocated in linker script
*/
void reserve_per_cpu_region(paddr *phy_kernel_end)
{
        u64 per_cpu_size = (u64)(&_per_cpu_end) - (u64)(&_per_cpu_start);
        __per_cpu_offset[0] = (u64)(&_per_cpu_start);
        *phy_kernel_end = ROUND_UP(*phy_kernel_end, 64);
        if (RENDEZVOS_MAX_CPU_NUMBER > 1)
                __per_cpu_offset[1] = KERNEL_PHY_TO_VIRT(*phy_kernel_end);
        *phy_kernel_end += (RENDEZVOS_MAX_CPU_NUMBER - 1) * per_cpu_size;
        calculate_per_cpu_offset();
}
void calculate_per_cpu_offset()
{
        u64 per_cpu_size = (u64)&_per_cpu_end - (u64)&_per_cpu_start;
        for (int i = 2; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                __per_cpu_offset[i] = __per_cpu_offset[i - 1] + per_cpu_size;
        }
}
void clean_per_cpu_region(paddr per_cpu_phy_addr)
{
        u64 per_cpu_size = (u64)&_per_cpu_end - (u64)&_per_cpu_start;
        memset((void *)KERNEL_PHY_TO_VIRT(per_cpu_phy_addr),
               '\0',
               (RENDEZVOS_MAX_CPU_NUMBER - 1) * per_cpu_size);
}