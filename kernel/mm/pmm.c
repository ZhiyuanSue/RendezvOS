#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <common/string.h>
#include <rendezvos/mm/buddy_pmm.h>
#include <modules/log/log.h>

struct memory_regions m_regions;
MemZone mem_zones[ZONE_NR_MAX];
DEFINE_PER_CPU(struct spin_lock_t, pmm_spin_lock[ZONE_NR_MAX]);
extern u64 L2_table, L1_table;
extern struct buddy buddy_pmm;
extern u64 _start, _end; /*the kernel end virt addr*/

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
int memory_regions_reserve_region_with_length(size_t length,
                                              u64 start_alignment,
                                              paddr *phy_start, paddr *phy_end)
{
        int used_region = -1;
        for (int i = 0; i < m_regions.region_count; i++) {
                if (m_regions.memory_regions_entry_empty(i))
                        continue;
                struct region *reg = &m_regions.memory_regions[i];
                paddr region_end = reg->addr + reg->len;
                if ((region_end - ROUND_UP(reg->addr, start_alignment))
                    > length) {
                        used_region = i;
                        *phy_start = ROUND_UP(reg->addr, start_alignment);
                        *phy_end = *phy_start + length;
                        /* if need , split */
                        if (reg->addr != *phy_start) {
                                m_regions.memory_regions_insert(
                                        reg->addr, *phy_start - reg->addr);
                        }
                        /* adjust this region's start */
                        reg->addr = *phy_end;
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
        .memory_regions_reserve_region_with_length =
                memory_regions_reserve_region_with_length,
        .memory_regions_init = memory_regions_init,
};
static inline void arch_map_percpu_data_space(paddr prev_region_phy_end,
                                              paddr percpu_phy_start,
                                              paddr percpu_phy_end)
{
        paddr percpu_phy_start_addr;
        paddr percpu_phy_start_addr_iter;
        paddr prev_phy_addr_round_up;
        ARCH_PFLAGS_t l2_flags;

        percpu_phy_start_addr = ROUND_DOWN(percpu_phy_start, MIDDLE_PAGE_SIZE);
        prev_phy_addr_round_up =
                ROUND_UP(prev_region_phy_end, MIDDLE_PAGE_SIZE);
        if (percpu_phy_start_addr < prev_phy_addr_round_up)
                percpu_phy_start_addr = prev_phy_addr_round_up;

        l2_flags = arch_decode_flags(
                2,
                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_HUGE | PAGE_ENTRY_READ
                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);
        for (percpu_phy_start_addr_iter = percpu_phy_start_addr;
             percpu_phy_start_addr_iter < percpu_phy_end;
             percpu_phy_start_addr_iter += MIDDLE_PAGE_SIZE) {
                arch_set_L2_entry(
                        percpu_phy_start_addr_iter,
                        KERNEL_PHY_TO_VIRT(percpu_phy_start_addr_iter),
                        (union L2_entry *)&L2_table,
                        l2_flags);
        }
}

static inline void arch_map_pmm_data_space(paddr prev_region_phy_end,
                                           paddr pmm_phy_start,
                                           paddr pmm_phy_end,
                                           paddr pmm_l2_start, u64 pmm_l2_pages)
{
        paddr pmm_phy_start_addr, pmm_phy_start_round_up_1g;
        paddr pmm_phy_start_addr_iter, pmm_phy_start_round_up_1g_iter,
                pmm_l2_start_iter;
        paddr prev_phy_addr_round_up;
        ARCH_PFLAGS_t l1_flags, l2_flags;

        pmm_phy_start_addr = ROUND_DOWN(pmm_phy_start, MIDDLE_PAGE_SIZE);
        prev_phy_addr_round_up =
                ROUND_UP(prev_region_phy_end, MIDDLE_PAGE_SIZE);
        if (pmm_phy_start_addr < prev_phy_addr_round_up)
                pmm_phy_start_addr = prev_phy_addr_round_up;

        pmm_phy_start_round_up_1g =
                ROUND_UP(pmm_phy_start_addr, HUGE_PAGE_SIZE);

        /*for we have mapped the 2m align space of kernel*/
        l2_flags = arch_decode_flags(
                2,
                PAGE_ENTRY_GLOBAL | PAGE_ENTRY_HUGE | PAGE_ENTRY_READ
                        | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);
        for (pmm_phy_start_addr_iter = pmm_phy_start_addr;
             pmm_phy_start_addr_iter < pmm_phy_end
             && pmm_phy_start_addr_iter < pmm_phy_start_round_up_1g;
             pmm_phy_start_addr_iter += MIDDLE_PAGE_SIZE) {
                /*As pmm and vmm part is not usable now, we still use boot page
                 * table*/
                arch_set_L2_entry(pmm_phy_start_addr_iter,
                                  KERNEL_PHY_TO_VIRT(pmm_phy_start_addr_iter),
                                  (union L2_entry *)&L2_table,
                                  l2_flags);
        }

        /*try to map the L1 table*/
        pmm_phy_start_round_up_1g_iter = pmm_phy_start_round_up_1g;
        pmm_l2_start_iter = pmm_l2_start;
        l1_flags = arch_decode_flags(1,
                                     PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                             | PAGE_ENTRY_VALID
                                             | PAGE_ENTRY_WRITE);
        for (; pmm_l2_start_iter < pmm_l2_start + pmm_l2_pages * PAGE_SIZE;
             pmm_l2_start_iter += PAGE_SIZE) {
                arch_set_L1_entry(
                        pmm_l2_start_iter,
                        KERNEL_PHY_TO_VIRT(pmm_phy_start_round_up_1g_iter),
                        (union L1_entry *)&L1_table,
                        l1_flags);
                pmm_phy_start_round_up_1g_iter += HUGE_PAGE_SIZE;
        }
        /*try to map the L2 tables under L1 table*/
        pmm_phy_start_round_up_1g_iter = pmm_phy_start_round_up_1g;
        pmm_l2_start_iter = pmm_l2_start;
        for (; pmm_l2_start_iter < pmm_l2_start + pmm_l2_pages * PAGE_SIZE;
             pmm_l2_start_iter += PAGE_SIZE) {
                for (pmm_phy_start_addr_iter = pmm_phy_start_round_up_1g_iter;
                     pmm_phy_start_addr_iter < pmm_phy_end
                     && pmm_phy_start_addr_iter < pmm_phy_start_round_up_1g_iter
                                                          + HUGE_PAGE_SIZE;
                     pmm_phy_start_addr_iter += MIDDLE_PAGE_SIZE) {
                        /*As pmm and vmm part is not usable now, we still use
                         * boot page table*/
                        arch_set_L2_entry(
                                pmm_phy_start_addr_iter,
                                KERNEL_PHY_TO_VIRT(pmm_phy_start_addr_iter),
                                (union L2_entry *)pmm_l2_start_iter,
                                l2_flags);
                }
                pmm_phy_start_round_up_1g_iter += HUGE_PAGE_SIZE;
        }
}
static inline void
calculate_avaliable_phy_addr_region(paddr *avaliable_phy_addr_start,
                                    paddr *avaliable_phy_addr_end,
                                    size_t *total_phy_page_frame_number)
{
        struct region reg;
        paddr sec_start_addr;
        paddr sec_end_addr;

        *avaliable_phy_addr_start = 0;
        if (!m_regions.memory_regions_entry_empty(0))
                *avaliable_phy_addr_start = m_regions.memory_regions[0].addr;
        *avaliable_phy_addr_start =
                ROUND_UP((*avaliable_phy_addr_start), PAGE_SIZE);

        *avaliable_phy_addr_end = 0;
        *total_phy_page_frame_number = 0;
        for (int i = 0; i < m_regions.region_count; i++) {
                if (m_regions.memory_regions_entry_empty(i))
                        continue;

                reg = m_regions.memory_regions[i];
                // pr_debug("Aviable Mem:base_phy_addr is 0x%x, length = "
                //         "0x%x\n",
                //         reg.addr,
                //         reg.len);
                /* end is not reachable,[ sec_end_addr , sec_end_addr ) */
                sec_start_addr = reg.addr;
                sec_end_addr = sec_start_addr + reg.len;

                if (sec_end_addr > *avaliable_phy_addr_end)
                        *avaliable_phy_addr_end = sec_end_addr;
                *total_phy_page_frame_number +=
                        (ROUND_UP(sec_end_addr, PAGE_SIZE)
                         - ROUND_DOWN(sec_start_addr, PAGE_SIZE))
                        / PAGE_SIZE;
        }
        *avaliable_phy_addr_end =
                ROUND_DOWN((*avaliable_phy_addr_end), PAGE_SIZE);
}
static inline void split_pmm_zones(paddr lower, paddr upper,
                                   size_t *total_section_number)
{
        MemZone *zone;
        struct region reg;
        paddr sec_start_addr;
        paddr sec_end_addr;
        *total_section_number = 0;
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                zone = &(mem_zones[mem_zone]);
                zone->lower_addr = 0;
                zone->upper_addr = 0;
                zone->zone_total_sections = 0;
                zone->zone_id = mem_zone;
                INIT_LIST_HEAD(&zone->section_list);
                switch (mem_zone) {
                        /*TODO:if we need more zones ,we can define zone upper
                         * and lower addr*/
                case ZONE_NORMAL:
                        zone->lower_addr = lower;
                        zone->upper_addr = upper;
                        zone->zone_total_pages = 0;
                        zone->pmm = (struct pmm *)&buddy_pmm;
                        buddy_pmm.zone = zone;
                        break;
                default:
                        break;
                }
                for (int i = 0; i < m_regions.region_count; i++) {
                        if (m_regions.memory_regions_entry_empty(i))
                                continue;
                        /*total 6 cases*/
                        reg = m_regions.memory_regions[i];
                        sec_start_addr = reg.addr;
                        sec_end_addr = sec_start_addr + reg.len;
                        size_t sec_size = 0;
                        if (sec_end_addr < zone->lower_addr
                            || sec_start_addr > zone->upper_addr)
                                continue;
                        sec_size = MIN(ROUND_DOWN(sec_end_addr, PAGE_SIZE),
                                       ROUND_DOWN(zone->upper_addr, PAGE_SIZE))
                                   - MAX(ROUND_UP(zone->lower_addr, PAGE_SIZE),
                                         ROUND_UP(sec_start_addr, PAGE_SIZE));
                        zone->zone_total_pages += sec_size / PAGE_SIZE;
                        zone->zone_total_sections++;
                }
                (*total_section_number) += zone->zone_total_sections;
        }
}

static inline size_t
calculate_sec_and_page_frame_pages(size_t total_phy_page_frame_number,
                                   size_t total_section_number)
{
        return ROUND_UP((total_phy_page_frame_number * sizeof(Page)
                         + total_section_number * sizeof(MemSection)),
                        PAGE_SIZE)
               / PAGE_SIZE;
}
static inline void calculate_pmm_space(u64 *total_pages, u64 *L2_table_pages)
{
        /*
           if the page frames need more than 1G space, we alloc more L2
           tables but a problem exist, the pmm start have a offset , and we are
           not sure of the offset, so it's hard to decide the real 1G spaces we
           need, we just add one page
        */
        size_t l2_pages = (*total_pages) / (HUGE_PAGE_SIZE / PAGE_SIZE) + 1;
        *L2_table_pages = l2_pages;
        *total_pages = *total_pages + l2_pages;
}
static inline error_t generate_zone_data(paddr zone_data_phy_start,
                                         paddr zone_data_phy_end)
{
        MemZone *zone;
        struct region reg;
        paddr sec_start_addr;
        paddr sec_end_addr;
        MemSection *sec;
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                zone = &(mem_zones[mem_zone]);
                zone->zone_total_pages = 0;
                u64 sec_id_count = 0;
                for (int i = 0; i < m_regions.region_count; i++) {
                        if (m_regions.memory_regions_entry_empty(i))
                                continue;
                        /*total 6 cases*/
                        reg = m_regions.memory_regions[i];
                        sec_start_addr = reg.addr;
                        sec_end_addr = sec_start_addr + reg.len;

                        if (sec_end_addr < zone->lower_addr
                            || sec_start_addr > zone->upper_addr)
                                continue;
                        sec = (MemSection *)(KERNEL_PHY_TO_VIRT(
                                zone_data_phy_start));
                        sec->upper_addr =
                                MIN(ROUND_DOWN(sec_end_addr, PAGE_SIZE),
                                    ROUND_DOWN(zone->upper_addr, PAGE_SIZE));
                        sec->lower_addr =
                                MAX(ROUND_UP(zone->lower_addr, PAGE_SIZE),
                                    ROUND_UP(sec_start_addr, PAGE_SIZE));
                        sec->page_count =
                                (sec->upper_addr - sec->lower_addr) / PAGE_SIZE;
                        zone->zone_total_pages += sec->page_count;
                        /*if the section have zero page, don't link to let
                         * search faster*/
                        if (sec->page_count) {
                                sec->sec_id = sec_id_count;
                                sec_id_count++;
                                list_add_head(&sec->section_list,
                                              &zone->section_list);
                        }
                        for (int i = 0; i < sec->page_count; i++) {
                                Page *p_ptr = Sec_phy_Page(sec, i);
                                if (p_ptr) {
                                        INIT_LIST_HEAD(&p_ptr->rmap_list);
                                        p_ptr->sec = sec;
                                }
                        }
                        zone_data_phy_start += sizeof(MemSection)
                                               + sec->page_count * sizeof(Page);
                }
        }
        if (zone_data_phy_start > zone_data_phy_end) {
                return -E_RENDEZVOS;
        }
        return 0;
}
void clean_pmm_region(paddr pmm_data_phy_start, paddr pmm_data_phy_end)
{
        memset((void *)pmm_data_phy_start,
               0,
               pmm_data_phy_end - pmm_data_phy_start);
}
error_t phy_mm_init(struct setup_info *arch_setup_info)
{
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start, per_cpu_phy_end;
        paddr pmm_data_phy_start;
        paddr pmm_data_phy_end;
        int kernel_region, pmm_region;
        arch_init_pmm(arch_setup_info, &per_cpu_phy_start);
        kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
        kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
        print("[ KERNEL_REGION\t@\t< 0x%x , 0x%x >]\n",
              (vaddr)(&_start),
              (vaddr)(&_end));
        /*
         * ===
         * reserve per cpu region after the kernel
         * ===
         */
        per_cpu_phy_end = per_cpu_phy_start;
        reserve_per_cpu_region(&per_cpu_phy_end);
        print("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(per_cpu_phy_start),
              KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        // adjust the memory regions, according to the kernel
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, per_cpu_phy_end);

        /*You need to check whether the kernel and percpu part have been
         * reserved all successfully*/
        if (kernel_region == -1) {
                print("cannot load kernel\n");
                goto init_pmm_error;
        }
        /*we hope the percpu and kernel are all in the same 1G,check it*/
        if (ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE)
            != ROUND_DOWN(per_cpu_phy_end, HUGE_PAGE_SIZE)) {
                print("cannot put percpu data and kernel data in the same 1G space\n");
                goto init_pmm_error;
        }
        arch_map_percpu_data_space(
                kernel_phy_end, per_cpu_phy_start, per_cpu_phy_end);

        clean_per_cpu_region(per_cpu_phy_start);
        /*
         * ===
         * reserve pmm manage region
         * ===
         */
        /*calculate the section and the page frame need space*/
        paddr avaliable_phy_start, avaliable_phy_end;
        size_t total_phy_page_frame_number, total_section_number;
        calculate_avaliable_phy_addr_region(&avaliable_phy_start,
                                            &avaliable_phy_end,
                                            &total_phy_page_frame_number);
        split_pmm_zones(
                avaliable_phy_start, avaliable_phy_end, &total_section_number);

        /*calculate the total section and phy page frame need pages */
        u64 zone_total_pages, pmm_total_pages, L2_table_pages;
        zone_total_pages = pmm_total_pages = calculate_sec_and_page_frame_pages(
                total_phy_page_frame_number, total_section_number);
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                MemZone *zone = &(mem_zones[mem_zone]);
                if (zone->pmm && zone->pmm->pmm_calculate_manage_space) {
                        zone->zone_pmm_manage_pages =
                                zone->pmm->pmm_calculate_manage_space(
                                        zone->zone_total_pages);
                        pmm_total_pages += zone->zone_pmm_manage_pages;
                }
        }
        calculate_pmm_space(&pmm_total_pages, &L2_table_pages);

        /*generate pmm position*/
        pmm_region = m_regions.memory_regions_reserve_region_with_length(
                pmm_total_pages * PAGE_SIZE,
                PAGE_SIZE,
                &pmm_data_phy_start,
                &pmm_data_phy_end);

        print("[ PMM_L2_TABLE\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start),
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start
                                 + L2_table_pages * PAGE_SIZE));
        print("[ PMM_DATA\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start
                                 + L2_table_pages * PAGE_SIZE),
              KERNEL_PHY_TO_VIRT(pmm_data_phy_end));
        if (pmm_region == -1) {
                print("cannot load the pmm data\n");
                goto init_pmm_error;
        }
        /*
                we have to reserve the following region ,
                let pmm not using this range.
                otherwise the pmm will try to map a level3 page
                into the pmm data map space (which using level 2 pages)
        */
        m_regions.memory_regions_reserve_region(
                pmm_data_phy_end, ROUND_UP(pmm_data_phy_end, MIDDLE_PAGE_SIZE));
        arch_map_pmm_data_space(per_cpu_phy_end,
                                pmm_data_phy_start,
                                pmm_data_phy_end,
                                pmm_data_phy_start,
                                L2_table_pages);
        /*we should also do not clean the pmm l2 table region*/
        paddr pmm_data_phy_start_offset =
                pmm_data_phy_start + L2_table_pages * PAGE_SIZE;
        clean_pmm_region(pmm_data_phy_start_offset, pmm_data_phy_end);
        /* === fill in the data === */
        if (generate_zone_data(pmm_data_phy_start_offset,
                               pmm_data_phy_start_offset
                                       + zone_total_pages * PAGE_SIZE)) {
                goto init_pmm_error;
        }
        /*generate the pmm data per zone*/
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                MemZone *zone = &(mem_zones[mem_zone]);
                if (zone->pmm && zone->pmm->pmm_init) {
                        zone->pmm->pmm_init(
                                zone->pmm,
                                pmm_data_phy_start_offset
                                        + zone_total_pages * PAGE_SIZE,
                                pmm_data_phy_start_offset
                                        + zone_total_pages * PAGE_SIZE
                                        + zone->zone_pmm_manage_pages
                                                  * PAGE_SIZE);
                }
        }
        return 0;
init_pmm_error:
        return (-E_RENDEZVOS);
}