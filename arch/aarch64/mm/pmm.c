#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/endianness.h>
#include <modules/dtb/dtb.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>

extern u64 _start, _end; /*the kernel end virt addr*/
extern u64 L2_table, L1_table;
extern struct memory_regions m_regions;

extern struct property_type property_types[PROPERTY_TYPE_NUM];
static void get_mem_prop_and_insert_region(struct fdt_property *fdt_prop)
{
        const char *data = (const char *)(fdt_prop->data);
        u_int32_t len = SWAP_ENDIANNESS_32(fdt_prop->len);
        u32 *u32_data = (u32 *)data;
        for (int index = 0; index < len; index += sizeof(u32) * 4) {
                u32 u32_1, u32_2, u32_3, u32_4;
                u64 addr, mem_len;
                u32_1 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                u32_2 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                addr = (((u64)u32_1) << 32) + u32_2;
                u32_3 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                u32_4 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                mem_len = (((u64)u32_3) << 32) + u32_4;
                print("[ Phy_Mem\t@\t< 0x%x , 0x%x >]\n", addr, addr + mem_len);
                m_regions.memory_regions_insert(addr, mem_len);
        }
}
static void arch_get_memory_regions(void *fdt)
{
        const char *memory_str = "memory\0";

        const char *device_type_str =
                property_types[PROPERTY_TYPE_DEVICE_TYPE].property_string;

        m_regions.memory_regions_init(&m_regions);
        raw_get_prop_from_dtb(fdt,
                              0,
                              property_types,
                              memory_str,
                              device_type_str,
                              0,
                              get_mem_prop_and_insert_region);
}
void arch_init_pmm(struct setup_info *arch_setup_info)
{
        struct fdt_header *dtb_header_ptr;
        paddr pmm_data_phy_start;
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start, per_cpu_phy_end;
        paddr pmm_data_phy_end;
        int kernel_region, pmm_region;
        struct fdt_reserve_entry *entry;

        /*
         * ===
         * get physical memory regions from platform description
         * ===
         */
        dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);

        arch_get_memory_regions(dtb_header_ptr);

        if (!m_regions.region_count)
                goto arch_init_pmm_error;

        kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
        kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
        print("[ KERNEL_REGION\t@\t< 0x%x , 0x%x >]\n",
              (vaddr)&_start,
              (vaddr)&_end);

        print("[ DTB_DATA\t@\t< 0x%x , 0x%x >]\n",
              ROUND_DOWN((vaddr)dtb_header_ptr, MIDDLE_PAGE_SIZE),
              ROUND_UP((vaddr)dtb_header_ptr + MIDDLE_PAGE_SIZE,
                       MIDDLE_PAGE_SIZE));
        for (u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap);
             off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct);
             off += sizeof(struct fdt_reserve_entry)) {
                entry = (struct fdt_reserve_entry *)((u64)dtb_header_ptr + off);
                if (entry->size) {
                        print("reserve_entry: address 0x%x size: 0x%x\n",
                              entry->address,
                              entry->size);
                }
        }
        /*
         * ===
         * reserve per cpu region after the kernel
         * ===
         */
        per_cpu_phy_start = per_cpu_phy_end =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        reserve_per_cpu_region(&per_cpu_phy_end);
        print("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
              arch_setup_info->map_end_virt_addr,
              KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        // adjust the memory regions, according to the kernel
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, per_cpu_phy_end);
        /*You need to check whether the kernel and dtb and percpu part have been
         * reserved all successfully*/
        if (kernel_region == -1) {
                print("cannot load kernel\n");
                goto arch_init_pmm_error;
        }
        /*we hope the percpu and kernel are all in the same 1G,check it*/
        if (ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE)
            != ROUND_DOWN(per_cpu_phy_end, HUGE_PAGE_SIZE)) {
                print("cannot put percpu data and kernel data in the same 1G space\n");
                goto arch_init_pmm_error;
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
        u64 pmm_total_pages, L2_table_pages;
        pmm_total_pages = calculate_sec_and_page_frame_pages(
                total_phy_page_frame_number, total_section_number);
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                MemZone *zone = &(mem_zones[mem_zone]);
                if (zone->pmm && zone->pmm->pmm_calculate_manage_space) {
                        zone->zone_pmm_manage_pages =
                                zone->pmm->pmm_calculate_manage_space(
                                        zone->zone_total_avaliable_pages);
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
                goto arch_init_pmm_error;
        }
        arch_map_pmm_data_space(per_cpu_phy_end,
                                pmm_data_phy_start,
                                pmm_data_phy_end,
                                pmm_data_phy_start,
                                L2_table_pages);
        clean_pmm_region(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                         pmm_data_phy_end);
        generate_pmm_data(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                          pmm_data_phy_end);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
