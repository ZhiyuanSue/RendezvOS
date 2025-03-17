#include <common/types.h>
#include <common/mm.h>
#include <common/platform/aarch64_qemu_virt.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <shampoos/mm/vmm.h>
extern void boot_Error();
/*take care of the vaddr and paddr ,here most are paddr*/
void boot_map_pg_table(u64 kernel_start_addr, u64 kernel_end_addr,
                       union L0_entry* L0_table_paddr,
                       union L1_entry* L1_table_paddr,
                       union L2_entry* L2_table_paddr,
                       union L3_entry* L3_table_paddr,
                       struct setup_info* setup_info_paddr)
{
        u64 kernel_start_page = ROUND_DOWN(kernel_start_addr, MIDDLE_PAGE_SIZE);
        u64 kernel_end_page = ROUND_UP(kernel_end_addr, MIDDLE_PAGE_SIZE);
        if ((kernel_start_page >> 30) != (kernel_end_page >> 30))
                boot_Error();
        /*L0*/
        arch_set_L0_entry((paddr)L1_table_paddr,
                          (vaddr)kernel_start_page,
                          L0_table_paddr,
                          PT_DESC_V | PT_DESC_BLOCK_OR_TABLE);
        /*L1*/
        arch_set_L1_entry((paddr)L2_table_paddr,
                          (vaddr)kernel_start_page,
                          L1_table_paddr,
                          PT_DESC_V | PT_DESC_BLOCK_OR_TABLE);
        /*L2*/
        for (vaddr page_start = kernel_start_page; page_start < kernel_end_page;
             page_start += MIDDLE_PAGE_SIZE) {
                arch_set_L2_entry((paddr)page_start,
                                  page_start,
                                  L2_table_paddr,
                                  PT_DESC_V | PT_DESC_ATTR_LOWER_AF);
        }
        setup_info_paddr->map_end_virt_addr =
                setup_info_paddr->boot_uart_base_addr =
                        KERNEL_PHY_TO_VIRT(kernel_end_page);
        /*L3*/
        arch_set_L2_entry((paddr)L3_table_paddr,
                          kernel_end_page,
                          L2_table_paddr,
                          PT_DESC_V | PT_DESC_BLOCK_OR_TABLE
                                  | PT_DESC_ATTR_LOWER_AF);
        arch_set_L3_entry(QEMU_VIRT_PL011_BASE_ADDR,
                          kernel_end_page,
                          L3_table_paddr,
                          PT_DESC_V | PT_DESC_PAGE | PT_DESC_ATTR_LOWER_AF);
        /*map virtual addr,update the L0 table*/
        arch_set_L0_entry((paddr)L1_table_paddr,
                          KERNEL_PHY_TO_VIRT(kernel_start_page),
                          L0_table_paddr,
                          PT_DESC_V | PT_DESC_BLOCK_OR_TABLE
                                  | PT_DESC_ATTR_LOWER_AF);
        setup_info_paddr->map_end_virt_addr = KERNEL_PHY_TO_VIRT(
                ROUND_UP(kernel_end_page + PAGE_SIZE, PAGE_SIZE));
}