#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <common/types.h>
#include <common/mm.h>
#include <modules/dtb/dtb.h>
#include <rendezvos/mm/vmm.h>
extern void boot_Error();

extern struct property_type property_types[255];
const char *uart_compatible = "arm,pl011\0";
/*
 * @brief before the virtual address space start,
 parser the dtb, which info is in setup_info_paddr,
 and get the uart start and len
 *
 * @param setup_info_paddr the setup info structure, including the dtb
 * @param uart_phy_addr the ptr that upper level function get the uart physical
 address
 * @uart_len the ptr that the upper level function get the uart len
 */
static void boot_get_uart_info(struct setup_info *setup_info_paddr,
                               u64 *uart_phy_addr, u64 *uart_len)
{
        struct property_type *property_types_paddr =
                (struct property_type *)(KERNEL_VIRT_TO_PHY(
                        (u64)(property_types)));
        const char *uart_compatible_phyaddr =
                (const char *)(KERNEL_VIRT_TO_PHY((u64)uart_compatible));
        const char *compatible_type_str =
                property_types_paddr[PROPERTY_TYPE_COMPATIBLE].property_string;
        struct fdt_property *uart_prop_ptr =
                raw_get_prop_from_dtb((void *)(setup_info_paddr->dtb_ptr),
                                      0,
                                      property_types_paddr,
                                      uart_compatible_phyaddr,
                                      compatible_type_str,
                                      DTB_RAW_GET_PROP_MODE_SINGLE,
                                      NULL);
        if (!uart_prop_ptr || *uart_len > MIDDLE_PAGE_SIZE)
                boot_Error(); /*not find uart, or uart len is tooo large*/

        const char *data = (const char *)(uart_prop_ptr->data);

        u_int32_t len = SWAP_ENDIANNESS_32(uart_prop_ptr->len);

        u32 *u32_data = (u32 *)data;
        for (u_int32_t index = 0; index < len; index += sizeof(u32) * 4) {
                u32 u32_1, u32_2, u32_3, u32_4;
                u32_1 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                u32_2 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                *uart_phy_addr = (((u64)u32_1) << 32) + u32_2;
                u32_3 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                u32_4 = SWAP_ENDIANNESS_32(*u32_data);
                u32_data++;
                *uart_len = (((u64)u32_3) << 32) + u32_4;
        }
}
/*
 * @brief in AArch64 boot, we need to build the page table before begin the vm
 *
 * @param kernel_start_addr the kernel start virtual addr decide by the linker script 
 * @param kernel_end_addr the kernel end virtual addr decide by the linker script
 * @param L0_table_paddr the level0 page table paddr
 * @param L1_table_paddr the level1 page table paddr
 * @param L2_table_paddr the level2 page table paddr
 * @param L3_table_paddr the level3 page table paddr
 * @param setup_info_paddr the setup info structure paddr
 * @note take care of the vaddr and paddr ,here most are paddr, we using the 4 level page system
 */
void boot_map_pg_table(u64 kernel_start_addr, u64 kernel_end_addr,
                       union L0_entry *L0_table_paddr,
                       union L1_entry *L1_table_paddr,
                       union L2_entry *L2_table_paddr,
                       union L3_entry *L3_table_paddr,
                       struct setup_info *setup_info_paddr)
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
        /*get the uart addr and len*/
        u64 uart_phy_addr = 0, uart_len = 0;
        boot_get_uart_info(setup_info_paddr, &uart_phy_addr, &uart_len);
        /*map the uart regs*/
        for (vaddr offset = 0; offset < uart_len; offset += PAGE_SIZE) {
                arch_set_L3_entry(uart_phy_addr + offset,
                                  kernel_end_page + offset,
                                  L3_table_paddr,
                                  PT_DESC_V | PT_DESC_PAGE
                                          | PT_DESC_ATTR_LOWER_AF);
        }
        setup_info_paddr->map_end_virt_addr = KERNEL_PHY_TO_VIRT(
                ROUND_UP(kernel_end_page + uart_len, PAGE_SIZE));
        /*map virtual addr,update the L0 table*/
        arch_set_L0_entry((paddr)L1_table_paddr,
                          KERNEL_PHY_TO_VIRT(kernel_start_page),
                          L0_table_paddr,
                          PT_DESC_V | PT_DESC_BLOCK_OR_TABLE
                                  | PT_DESC_ATTR_LOWER_AF);
        /*mov the dtb data behind the end of the kernel*/
        u8 *dst_dtb_paddr =
                (u8 *)ROUND_UP(kernel_end_page + uart_len, MIDDLE_PAGE_SIZE);
        u8 *src_dtb_paddr = (u8 *)setup_info_paddr->dtb_ptr;
        setup_info_paddr->dtb_ptr = (u64)dst_dtb_paddr;
        /*we need to consider the order of memcpy*/
        if (dst_dtb_paddr < src_dtb_paddr) {
                for (u64 iter = 0; iter < MIDDLE_PAGE_SIZE; iter++)
                        *dst_dtb_paddr++ = *src_dtb_paddr++;
        } else if (dst_dtb_paddr > src_dtb_paddr) {
                dst_dtb_paddr += MIDDLE_PAGE_SIZE;
                src_dtb_paddr += MIDDLE_PAGE_SIZE;
                for (u64 iter = 0; iter < MIDDLE_PAGE_SIZE; iter++)
                        *dst_dtb_paddr-- = *src_dtb_paddr--;
        }
}