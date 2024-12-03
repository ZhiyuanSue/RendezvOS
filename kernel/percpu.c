#include <shampoos/limits.h>
#include <shampoos/percpu.h>
#include <shampoos/mm/vmm.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(int, cpu_number);
extern char *_per_cpu_end, _per_cpu_start;

u64 __per_cpu_offset[SHAMPOOS_MAX_CPU_NUMBER];

static void map_per_cpu_region(paddr per_cpu_start, paddr per_cpu_end)
{
}
/*
the phy position of per cpu data is placed between kernel end and the pmm data
we only use SHAMPOOS_MAX_CPU_NUMBER-1
because the index 0 is using the space allocated in linker script
*/
void reserve_per_cpu_region(paddr* phy_kernel_end)
{
        u64 per_cpu_size = (u64)(&_per_cpu_end) - (u64)(&_per_cpu_start);
        paddr per_cpu_start, per_cpu_end;
        __per_cpu_offset[0] = 0;
        per_cpu_start = *phy_kernel_end = ROUND_UP(*phy_kernel_end, 64);
        __per_cpu_offset[1] =
                KERNEL_PHY_TO_VIRT(*phy_kernel_end) - (u64)_per_cpu_start;
        *phy_kernel_end += (SHAMPOOS_MAX_CPU_NUMBER - 1) * per_cpu_size;
        per_cpu_end = *phy_kernel_end;
        map_per_cpu_region(per_cpu_start, per_cpu_end);
}
void calculate_per_cpu_offset()
{
        u64 per_cpu_size = (u64)&_per_cpu_end - (u64)&_per_cpu_start;
        for (int i = 2; i < SHAMPOOS_MAX_CPU_NUMBER; i++) {
                __per_cpu_offset[i] = __per_cpu_offset[i - 1] + per_cpu_size;
        }
}