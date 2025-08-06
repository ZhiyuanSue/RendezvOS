#include <rendezvos/error.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
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

extern u64 L2_table, L1_table;
void arch_map_extra_data_space(paddr kernel_phy_start, paddr kernel_phy_end,
                               paddr extra_data_phy_start,
                               paddr extra_data_phy_end, paddr pmm_l2_start,
                               u64 pmm_l2_pages)
{
        paddr extra_data_phy_start_addr, extra_data_phy_start_round_up_1g;
        paddr extra_data_phy_start_addr_iter,
                extra_data_phy_start_round_up_1g_iter, pmm_l2_start_iter;
        paddr kernel_end_phy_addr_round_up;
        ARCH_PFLAGS_t l1_flags, l2_flags;

        extra_data_phy_start_addr =
                ROUND_DOWN(extra_data_phy_start, MIDDLE_PAGE_SIZE);
        kernel_end_phy_addr_round_up =
                ROUND_UP(kernel_phy_end, MIDDLE_PAGE_SIZE);
        if (extra_data_phy_start_addr < kernel_end_phy_addr_round_up)
                extra_data_phy_start_addr = kernel_end_phy_addr_round_up;

        extra_data_phy_start_round_up_1g =
                ROUND_UP(extra_data_phy_start_addr, HUGE_PAGE_SIZE);

        /*for we have mapped the 2m align space of kernel*/
        l2_flags = arch_decode_flags(
                2,
                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_HUGE | PAGE_ENTRY_READ
                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);
        for (extra_data_phy_start_addr_iter = extra_data_phy_start_addr;
             extra_data_phy_start_addr_iter < extra_data_phy_end
             && extra_data_phy_start_addr_iter
                        < extra_data_phy_start_round_up_1g;
             extra_data_phy_start_addr_iter += MIDDLE_PAGE_SIZE) {
                /*As pmm and vmm part is not usable now, we still use boot page
                 * table*/
                arch_set_L2_entry(
                        extra_data_phy_start_addr_iter,
                        KERNEL_PHY_TO_VIRT(extra_data_phy_start_addr_iter),
                        (union L2_entry *)&L2_table,
                        l2_flags);
        }

        /*try to map the L1 table*/
        extra_data_phy_start_round_up_1g_iter =
                extra_data_phy_start_round_up_1g;
        pmm_l2_start_iter = pmm_l2_start;
        l1_flags = arch_decode_flags(1,
                                     PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                             | PAGE_ENTRY_VALID
                                             | PAGE_ENTRY_WRITE);
        for (; pmm_l2_start_iter < pmm_l2_start + pmm_l2_pages * PAGE_SIZE;
             pmm_l2_start_iter += PAGE_SIZE) {
                arch_set_L1_entry(
                        pmm_l2_start_iter,
                        KERNEL_PHY_TO_VIRT(
                                extra_data_phy_start_round_up_1g_iter),
                        (union L1_entry *)&L1_table,
                        l1_flags);
                extra_data_phy_start_round_up_1g_iter += HUGE_PAGE_SIZE;
        }
        /*try to map the L2 tables under L1 table*/
        extra_data_phy_start_round_up_1g_iter =
                extra_data_phy_start_round_up_1g;
        pmm_l2_start_iter = pmm_l2_start;
        for (; pmm_l2_start_iter < pmm_l2_start + pmm_l2_pages * PAGE_SIZE;
             pmm_l2_start_iter += PAGE_SIZE) {
                for (extra_data_phy_start_addr_iter =
                             extra_data_phy_start_round_up_1g_iter;
                     extra_data_phy_start_addr_iter < extra_data_phy_end
                     && extra_data_phy_start_addr_iter
                                < extra_data_phy_start_round_up_1g_iter
                                          + HUGE_PAGE_SIZE;
                     extra_data_phy_start_addr_iter += MIDDLE_PAGE_SIZE) {
                        /*As pmm and vmm part is not usable now, we still use
                         * boot page table*/
                        arch_set_L2_entry(
                                extra_data_phy_start_addr_iter,
                                KERNEL_PHY_TO_VIRT(
                                        extra_data_phy_start_addr_iter),
                                (union L2_entry *)pmm_l2_start_iter,
                                l2_flags);
                }
                extra_data_phy_start_round_up_1g_iter += HUGE_PAGE_SIZE;
        }
}