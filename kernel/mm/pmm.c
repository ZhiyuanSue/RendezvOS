#include <shampoos/error.h>
#include <shampoos/mm/pmm.h>

struct memory_regions m_regions;
error_t memory_regions_insert(paddr addr, u64 len)
{
        if (m_regions.region_count >= SHAMPOOS_MAX_MEMORY_REGIONS)
                return (-ENOMEM);
        m_regions.memory_regions[m_regions.region_count].addr = addr;
        m_regions.memory_regions[m_regions.region_count].len = len;
        m_regions.region_count++;
        return (0);
}
void memory_regions_delete(int index)
{
        // I don't realize a vector or stack, so just make it empty
        m_regions.memory_regions[index].addr = 0;
        m_regions.memory_regions[index].len = 0;
}
bool memory_regions_entry_empty(int index)
{
        return (m_regions.memory_regions[index].addr == 0
                && m_regions.memory_regions[index].len == 0);
}
struct memory_regions m_regions = {
        .memory_regions_insert = memory_regions_insert,
        .memory_regions_delete = memory_regions_delete,
        .memory_regions_entry_empty = memory_regions_entry_empty,
};