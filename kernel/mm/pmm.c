#include <rendezvos/error.h>
#include <rendezvos/mm/pmm.h>
#include <common/string.h>

struct memory_regions m_regions;
error_t memory_regions_insert(paddr addr, u64 len)
{
        if (m_regions.region_count >= RENDEZVOS_MAX_MEMORY_REGIONS)
                return (-E_IN_PARAM);
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
int memory_regions_reserve_region(paddr phy_start, paddr phy_end)
{
        int used_region = -1;
        for (int i = 0; i < m_regions.region_count; i++) {
                if (m_regions.memory_regions_entry_empty(i))
                        continue;
                struct region *reg = &m_regions.memory_regions[i];
                paddr region_end = reg->addr + reg->len;
                // find the region
                bool need_adjust = false;
                if (phy_start >= reg->addr && phy_end <= region_end) {
                        need_adjust = true;
                } else if (phy_start >= reg->addr && phy_start <= region_end) {
                        need_adjust = true;
                        phy_end = region_end;
                } else if (phy_end >= reg->addr && phy_end <= region_end) {
                        need_adjust = true;
                        phy_start = reg->addr;
                }
                if (need_adjust) {
                        // the reserved memory used all the memeory
                        if (phy_start == reg->addr && phy_end == region_end)
                                m_regions.memory_regions_delete(i);
                        // only one size is used, just change the region
                        else if (phy_start == reg->addr) {
                                reg->len = reg->len - phy_end + reg->addr;
                                reg->addr = phy_end;
                        } else if (phy_end == region_end) {
                                reg->len = phy_start - reg->addr;
                        } else {
                                // both side have space, adjust the region and
                                // insert a new one
                                m_regions.memory_regions_insert(
                                        reg->addr, phy_start - reg->addr);
                                reg->addr = phy_end;
                                reg->len = region_end - phy_end;
                        }
                        used_region = i;
                        break;
                }
        }
        return used_region;
}
void memory_regions_init(struct memory_regions *m_regions)
{
        m_regions->region_count = 0;
        memset((void *)(m_regions->memory_regions),
               0,
               sizeof(struct region) * RENDEZVOS_MAX_MEMORY_REGIONS);
}

struct memory_regions m_regions = {
        .memory_regions_insert = memory_regions_insert,
        .memory_regions_delete = memory_regions_delete,
        .memory_regions_entry_empty = memory_regions_entry_empty,
        .memory_regions_reserve_region = memory_regions_reserve_region,
        .memory_regions_init = memory_regions_init,
};