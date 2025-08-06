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
        raw_get_prop_from_dtb(fdt,
                              0,
                              property_types,
                              memory_str,
                              device_type_str,
                              0,
                              get_mem_prop_and_insert_region);
}

static void arch_map_extra_data_space(paddr kernel_phy_start,
                                      paddr kernel_phy_end,
                                      paddr extra_data_phy_start,
                                      paddr extra_data_phy_end,
                                      paddr pmm_l2_start, u64 pmm_l2_pages)
{
        paddr extra_data_phy_start_addr, extra_data_phy_start_round_up_1g;
        paddr extra_data_phy_start_addr_iter,
                extra_data_phy_start_round_up_1g_iter, pmm_l2_start_iter;
        paddr kernel_end_phy_addr_round_up;
        ARCH_PFLAGS_t l1_flags, l2_flags;

        extra_data_phy_start_addr = extra_data_phy_start;
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

void arch_init_pmm(struct setup_info *arch_setup_info)
{
        struct fdt_header *dtb_header_ptr;
        paddr pmm_data_phy_start;
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start, per_cpu_phy_end;
        paddr pmm_data_phy_end;
        int kernel_region;
        struct fdt_reserve_entry *entry;
        paddr map_end_phy_addr;

        dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);

        m_regions.memory_regions_init(&m_regions);
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

        per_cpu_phy_start = per_cpu_phy_end =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        reserve_per_cpu_region(&per_cpu_phy_end);
        print("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
              arch_setup_info->map_end_virt_addr,
              KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        pmm_data_phy_start = ROUND_UP(per_cpu_phy_end, PAGE_SIZE);

        pmm_data_phy_end = pmm_data_phy_start;
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

        // adjust the memory regions, according to the kernel
        map_end_phy_addr =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, map_end_phy_addr);
        /*You need to check whether the kernel and dtb have been loaded all
         * successfully*/
        if (kernel_region == -1) {
                print("cannot load kernel\n");
                goto arch_init_pmm_error;
        }

        u64 pmm_total_pages, L2_table_pages;
        calculate_pmm_space(&pmm_total_pages, &L2_table_pages);
        pmm_data_phy_end += pmm_total_pages * PAGE_SIZE;
        print("[ PMM_L2_TABLE\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start),
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start
                                 + L2_table_pages * PAGE_SIZE));
        print("[ PMM_DATA\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(pmm_data_phy_start
                                 + L2_table_pages * PAGE_SIZE),
              KERNEL_PHY_TO_VIRT(pmm_data_phy_end));
        if (m_regions.memory_regions[kernel_region].addr
                    + m_regions.memory_regions[kernel_region].len
            < pmm_data_phy_end) {
                print("cannot load the pmm_data\n");
                goto arch_init_pmm_error;
        }
        arch_map_extra_data_space(kernel_phy_start,
                                  kernel_phy_end,
                                  per_cpu_phy_start,
                                  pmm_data_phy_end,
                                  pmm_data_phy_start,
                                  L2_table_pages);
        clean_per_cpu_region(per_cpu_phy_start);
        clean_pmm_region(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                         pmm_data_phy_end);
        generate_pmm_data(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                          pmm_data_phy_end);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
