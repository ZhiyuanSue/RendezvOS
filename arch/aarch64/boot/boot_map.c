#include <common/types.h>
#include <common/mm.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <rendezvos/mm/vmm.h>
#include <modules/dtb/dtb.h>
extern void boot_Error();

extern struct property_type property_types[255];
const char *uart_compatible = "arm,pl011\0";
bool boot_get_uart_info(void *fdt, int offset, int depth, u64 *uart_phy_addr,
                        u64 *uart_len)
{
        struct property_type *property_types_paddr =
                (struct property_type *)(KERNEL_VIRT_TO_PHY(
                        (u64)(property_types)));
        const char *uart_compatible_phyaddr =
                (const char *)(KERNEL_VIRT_TO_PHY((u64)uart_compatible));
        const char *compatible_type_str =
                property_types_paddr[PROPERTY_TYPE_COMPATIBLE].property_string;
        const char *reg_str =
                property_types_paddr[PROPERTY_TYPE_REG].property_string;
        struct fdt_property *prop;
        const char *property_name;
        const char *data;
        const char *s;
        u_int32_t len;
        u32 *u32_data;

        int property, node;
        fdt_for_each_property_offset(property, fdt, offset)
        {
                prop = (struct fdt_property *)fdt_offset_ptr(
                        fdt, property, FDT_TAGSIZE);
                property_name =
                        fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
                data = (const char *)(prop->data);
                if (!strcmp(property_name, compatible_type_str)
                    && !strcmp_s(data,
                                 uart_compatible_phyaddr,
                                 strlen(uart_compatible_phyaddr))) {
                        goto find_node;
                }
        }
        fdt_for_each_subnode(node, fdt, offset)
        {
                if (boot_get_uart_info(
                            fdt, node, depth + 1, uart_phy_addr, uart_len))
                        return true;
        }
        return false;
find_node:
        fdt_for_each_property_offset(property, fdt, offset)
        {
                prop = (struct fdt_property *)fdt_offset_ptr(
                        fdt, property, FDT_TAGSIZE);
                s = fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
                data = (const char *)(prop->data);
                len = SWAP_ENDIANNESS_32(prop->len);
                if (!strcmp(s, reg_str)) {
                        u32_data = (u32 *)data;
                        for (int index = 0; index < len;
                             index += sizeof(u32) * 4) {
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
                        return true;
                }
        }
        return false;
}
/*take care of the vaddr and paddr ,here most are paddr*/
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
        u64 uart_phy_addr, uart_len;
        if (!boot_get_uart_info((void *)(setup_info_paddr->dtb_ptr),
                                0,
                                0,
                                &uart_phy_addr,
                                &uart_len)
            || uart_len > MIDDLE_PAGE_SIZE)
                boot_Error(); /*not find uart, or uart len is tooo large*/
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
}