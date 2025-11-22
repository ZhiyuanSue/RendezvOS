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

extern u64 L2_table, L1_table;
extern struct memory_regions m_regions;

extern struct property_type property_types[PROPERTY_TYPE_NUM];
static void get_mem_prop_and_insert_region(struct fdt_property *fdt_prop)
{
        const char *data = (const char *)(fdt_prop->data);
        u_int32_t len = SWAP_ENDIANNESS_32(fdt_prop->len);
        u32 *u32_data = (u32 *)data;
        for (u_int32_t index = 0; index < len; index += sizeof(u32) * 4) {
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
void arch_init_pmm(struct setup_info *arch_setup_info,
                   vaddr *next_region_phy_start)
{
        struct fdt_header *dtb_header_ptr;
        struct fdt_reserve_entry *entry;

        dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);

        arch_get_memory_regions(dtb_header_ptr);

        if (!m_regions.region_count)
                goto arch_init_pmm_error;

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
        *next_region_phy_start =
                KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
