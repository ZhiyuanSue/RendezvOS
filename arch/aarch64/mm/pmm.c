#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/endianness.h>
#include <modules/dtb/dtb.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/percpu.h>

extern char _start, _end; /*the kernel end virt addr*/
extern u64 L0_table, L1_table, L2_table;
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
                pr_info("region start 0x%x,len 0x%x\n", addr, mem_len);
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
                                      paddr extra_data_phy_end)
{
        paddr extra_data_phy_start_addr;
        paddr kernel_end_phy_addr_round_up;
        paddr extra_data_start_round_down_2m;
        ARCH_PFLAGS_t flags;

        extra_data_phy_start_addr = extra_data_phy_start;
        kernel_end_phy_addr_round_up =
                ROUND_UP(kernel_phy_end, MIDDLE_PAGE_SIZE);
        if (extra_data_phy_start_addr < kernel_end_phy_addr_round_up)
                extra_data_phy_start_addr = kernel_end_phy_addr_round_up;
        /*for we have mapped the 2m align space of kernel*/
        flags = arch_decode_flags(2,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_HUGE
                                          | PAGE_ENTRY_READ | PAGE_ENTRY_VALID
                                          | PAGE_ENTRY_WRITE);
        for (; extra_data_phy_start_addr < extra_data_phy_end;
             extra_data_phy_start_addr += MIDDLE_PAGE_SIZE) {
                /*As pmm and vmm part is not usable now, we still use boot page
                 * table*/
                extra_data_start_round_down_2m =
                        ROUND_DOWN(extra_data_phy_start_addr, MIDDLE_PAGE_SIZE);
                arch_set_L2_entry(
                        extra_data_start_round_down_2m,
                        KERNEL_PHY_TO_VIRT(extra_data_start_round_down_2m),
                        (union L2_entry *)&L2_table,
                        flags);
        }
}

void arch_init_pmm(struct setup_info *arch_setup_info)
{
        struct fdt_header *dtb_header_ptr;
        paddr pmm_data_phy_start;
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start;
        paddr pmm_data_phy_end;
        int kernel_region;
        struct fdt_reserve_entry *entry;
        paddr map_end_phy_addr;

        dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);
        per_cpu_phy_start =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        reserve_per_cpu_region(&per_cpu_phy_start);
        pmm_data_phy_start = ROUND_UP(per_cpu_phy_start, PAGE_SIZE);
        per_cpu_phy_start =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
        kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
        pmm_data_phy_end = 0;
        pr_info("start arch init pmm\n");
        for (u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap);
             off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct);
             off += sizeof(struct fdt_reserve_entry)) {
                entry = (struct fdt_reserve_entry *)((u64)dtb_header_ptr + off);
                pr_info("reserve_entry: address 0x%x size: 0x%x\n",
                        entry->address,
                        entry->size);
        }
        m_regions.region_count = 0;
        arch_get_memory_regions(dtb_header_ptr);
        if (!m_regions.region_count)
                goto arch_init_pmm_error;
        // adjust the memory regions, according to the kernel
        map_end_phy_addr =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, map_end_phy_addr);
        /*You need to check whether the kernel and dtb have been loaded all
         * successfully*/
        if (kernel_region == -1) {
                pr_info("cannot load kernel\n");
                goto arch_init_pmm_error;
        }
        pmm_data_phy_end =
                pmm_data_phy_start + calculate_pmm_space() * PAGE_SIZE;
        pr_info("pmm_data start 0x%x end 0x%x\n",
                pmm_data_phy_start,
                pmm_data_phy_end);
        if (ROUND_DOWN(pmm_data_phy_end, HUGE_PAGE_SIZE)
            != ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE)) {
                pr_error("cannot load the pmm data\n");
                goto arch_init_pmm_error;
        }
        if (m_regions.memory_regions[kernel_region].addr
                    + m_regions.memory_regions[kernel_region].len
            < pmm_data_phy_end) {
                pr_error("cannot load the pmm_data\n");
                goto arch_init_pmm_error;
        }
        arch_map_extra_data_space(kernel_phy_start,
                                  kernel_phy_end,
                                  per_cpu_phy_start,
                                  pmm_data_phy_end);
        clean_per_cpu_region(per_cpu_phy_start);
        generate_pmm_data(kernel_phy_start,
                          kernel_phy_end,
                          pmm_data_phy_start,
                          pmm_data_phy_end);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
