#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/mm/vmm.h>
#include <arch/x86_64/power_ctrl.h>
#include <modules/log/log.h>
#include <modules/acpi/acpi.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>

extern char _start, _end; /*the kernel end virt addr*/
extern u64 L2_table, L1_table;
extern struct memory_regions m_regions;

#define multiboot_insert_memory_region(mmap)                                    \
        if (mmap->addr + mmap->len > BIOS_MEM_UPPER                             \
            && mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {                      \
                if (m_regions.memory_regions_insert(mmap->addr, mmap->len)) {   \
                        print("we cannot manager toooo many memory regions\n"); \
                        goto arch_init_pmm_error;                               \
                } else {                                                        \
                        print("[ Phy_Mem\t@\t< 0x%x , 0x%x >]\n",               \
                              mmap->addr,                                       \
                              mmap->len);                                       \
                }                                                               \
        }

static error_t arch_get_memory_regions(struct setup_info *arch_setup_info)
{
        u32 mtb_magic;
        struct multiboot_info *mtb_info;

        struct multiboot2_info *mtb2_info;

        vaddr addr_ptr;
        /*some ptr is phisical, we need to change it to virtual*/

        mtb_magic = arch_setup_info->multiboot_magic;
        if (mtb_magic == MULTIBOOT_MAGIC) {
                /*multiboot 1 memory region detect*/
                mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
                addr_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
                u64 length = mtb_info->mmap.mmap_length;
                /* check the multiboot header */
                if (!MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                               MULTIBOOT_INFO_FLAG_MEM)
                    || !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                                  MULTIBOOT_INFO_FLAG_MMAP)) {
                        print("no mem info\n");
                        goto arch_init_pmm_error;
                }
                /*generate the memory region info*/
                m_regions.memory_regions_init(&m_regions);

                for_each_multiboot_mmap(addr_ptr, length)
                {
                        multiboot_insert_memory_region(mmap);
                }
        } else if (mtb_magic == MULTIBOOT2_MAGIC) {
                mtb2_info = GET_MULTIBOOT2_INFO(arch_setup_info);
                for_each_tag(mtb2_info)
                {
                        switch (tag->type) {
                        case MULTIBOOT2_TAG_TYPE_MMAP: {
                                addr_ptr =
                                        (vaddr)(((struct multiboot2_tag_mmap *)
                                                         tag)
                                                        ->entries);
                                for_each_multiboot2_mmap(tag, addr_ptr)
                                {
                                        multiboot_insert_memory_region(mmap);
                                }
                        } break;
                        }
                }
        }
        return (0);
arch_init_pmm_error:
        return (-E_RENDEZVOS);
}
error_t reserve_arch_region(struct setup_info *arch_setup_info)
{
        // reserve acpi region
        struct acpi_table_rsdp *rsdp_table =
                acpi_probe_rsdp(KERNEL_VIRT_OFFSET);
        if (!rsdp_table) {
                print("not find any rsdp\n");
                return 0;
        }
        // print("====== rsdp @[0x%x]] ======\n", rsdp_table);
        arch_setup_info->rsdp_addr = (vaddr)rsdp_table;
        if (rsdp_table->revision == 0) {
                // print("====== rsdt @[0x%x]] ======\n",
                // rsdp_table->rsdt_address);
                paddr acpi_reserve_phy_start =
                        ROUND_DOWN(rsdp_table->rsdt_address, MIDDLE_PAGE_SIZE);
                paddr acpi_reserve_phy_end =
                        acpi_reserve_phy_start + MIDDLE_PAGE_SIZE;
                int acpi_region = m_regions.memory_regions_reserve_region(
                        acpi_reserve_phy_start, acpi_reserve_phy_end);
                print("[ ACPI_DATA\t@\t< 0x%x , 0x%x >]\n",
                      KERNEL_PHY_TO_VIRT(acpi_reserve_phy_start),
                      KERNEL_PHY_TO_VIRT(acpi_reserve_phy_end));
                if (acpi_region == -1) {
                        print("cannot load acpi\n");
                        return -E_RENDEZVOS;
                }
        } else {
                print("[ ACPI ] unsupported vision: %d\n",
                      rsdp_table->revision);
                return -E_RENDEZVOS;
        }
        return 0;
}
void arch_init_pmm(struct setup_info *arch_setup_info)
{
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start, per_cpu_phy_end;
        paddr pmm_data_phy_start;
        paddr pmm_data_phy_end;
        int kernel_region, pmm_region;

        kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
        kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
        /*
         * ===
         * get physical memory regions from platform description
         * ===
         */
        if (arch_get_memory_regions(arch_setup_info) < 0)
                goto arch_init_pmm_error;

        if (!m_regions.region_count)
                goto arch_init_pmm_error;

        /*
         * ===
         * reserve per cpu region after the kernel
         * ===
         */
        per_cpu_phy_start = per_cpu_phy_end = kernel_phy_end;
        reserve_per_cpu_region(&per_cpu_phy_end);

        // adjust the memory regions, according to the kernel
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, per_cpu_phy_end);

        print("[ KERNEL_REGION\t@\t< 0x%x , 0x%x >]\n",
              (vaddr)(&_start),
              (vaddr)(&_end));
        print("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(per_cpu_phy_start),
              KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        /*You need to check whether the arch reserve regions have been loaded
         * all successfully*/
        if (reserve_arch_region(arch_setup_info) < 0) {
                print("cannot reserve arch region\n");
                goto arch_init_pmm_error;
        }
        /*You need to check whether the kernel and percpu part have been
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
        u64 zone_total_pages, pmm_total_pages, L2_table_pages;
        zone_total_pages = pmm_total_pages = calculate_sec_and_page_frame_pages(
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
        /*we should also do not clean the pmm l2 table region*/
        paddr pmm_data_phy_start_offset =
                pmm_data_phy_start + L2_table_pages * PAGE_SIZE;
        clean_pmm_region(pmm_data_phy_start_offset, pmm_data_phy_end);

        /* === fill in the data === */
        if (generate_zone_data(pmm_data_phy_start_offset,
                               pmm_data_phy_start_offset + zone_total_pages)) {
                goto arch_init_pmm_error;
        }
        /*before we generate the pmm data per zone, we must mark pmm data pages as used*/
        mark_pmm_data_as_used(pmm_data_phy_start, pmm_data_phy_end);
        /*generate the pmm data per zone*/
        for (int mem_zone = 0; mem_zone < ZONE_NR_MAX; ++mem_zone) {
                MemZone *zone = &(mem_zones[mem_zone]);
                if (zone->pmm && zone->pmm->pmm_generate_data) {
                        zone->pmm->pmm_generate_data(
                                zone->pmm,
                                pmm_data_phy_start_offset + zone_total_pages,
                                pmm_data_phy_start_offset + zone_total_pages
                                        + zone->zone_pmm_manage_pages);
                }
        }
        return;
arch_init_pmm_error:
        arch_shutdown();
}
