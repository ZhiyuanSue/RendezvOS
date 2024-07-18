#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/endianness.h>
#include <modules/dtb/dtb.h>
#include <shampoos/limits.h>
#include <shampoos/mm/buddy_pmm.h>
extern char _start, _end; /*the kernel end virt addr*/
extern u64 L0_table, L1_table, L2_table;
extern struct buddy buddy_pmm;

extern struct property_type property_types[PROPERTY_TYPE_NUM];
extern u64 entry_per_bucket[BUDDY_MAXORDER + 1],
	pages_per_bucket[BUDDY_MAXORDER + 1];
static void arch_get_memory_regions(void *fdt, int offset, int depth) {
	/*
		actually we seems to use something like of_find_node_by_type
		but now we have no memory to alloc any struct of device_node
	*/
	int property, node;
	const char *device_type_str =
		property_types[PROPERTY_TYPE_DEVICE_TYPE].property_string;
	const char *memory_str = "memory\0";
	const char *reg_str = property_types[PROPERTY_TYPE_REG].property_string;
	fdt_for_each_property_offset(property, fdt, offset) {
		struct fdt_property *prop =
			(struct fdt_property *)fdt_offset_ptr(fdt, property, FDT_TAGSIZE);
		const char *property_name =
			fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
		const char *data = (const char *)(prop->data);
		if (!strcmp(property_name, device_type_str) &&
			!strcmp(data, memory_str)) {
			goto find_memory_node;
		}
	}
	fdt_for_each_subnode(node, fdt, offset) {
		arch_get_memory_regions(fdt, node, depth + 1);
	}
	return;
find_memory_node:
	fdt_for_each_property_offset(property, fdt, offset) {
		struct fdt_property *prop =
			(struct fdt_property *)fdt_offset_ptr(fdt, property, FDT_TAGSIZE);
		const char *s = fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
		const char *data = (const char *)(prop->data);
		u_int32_t len = SWAP_ENDIANNESS_32(prop->len);
		if (!strcmp(s, reg_str)) {
			u32 *u32_data = (u32 *)data;
			for (int index = 0; index < len; index += sizeof(u32) * 4) {
				u32 u32_1, u32_2, u32_3, u32_4;
				u32 addr, len;
				u32_1 = SWAP_ENDIANNESS_32(*u32_data);
				u32_data++;
				u32_2 = SWAP_ENDIANNESS_32(*u32_data);
				u32_data++;
				addr = u32_1 + u32_2;
				u32_3 = SWAP_ENDIANNESS_32(*u32_data);
				u32_data++;
				u32_4 = SWAP_ENDIANNESS_32(*u32_data);
				u32_data++;
				len = u32_3 + u32_4;
				pr_info("region start 0x%x,len 0x%x\n", addr, len);
				buddy_pmm.m_regions->memory_regions_insert(addr, len);
			}
		}
	}
}

static void arch_map_buddy_data_space(paddr kernel_phy_start,
									  paddr kernel_phy_end,
									  paddr buddy_phy_start,
									  paddr buddy_phy_end) {
	paddr buddy_phy_start_addr = buddy_phy_start;
	paddr kernel_end_phy_addr_round_up =
		ROUND_UP(kernel_phy_end, MIDDLE_PAGE_SIZE);
	if (buddy_phy_start_addr < kernel_end_phy_addr_round_up)
		buddy_phy_start_addr =
			kernel_end_phy_addr_round_up; /*for we have mapped the 2m align
											 space of kernel*/
	for (; buddy_phy_start_addr < buddy_phy_end;
		 buddy_phy_start_addr += MIDDLE_PAGE_SIZE) {
		/*As pmm and vmm part is not usable now, we still use boot page table*/
		paddr buddy_start_round_down_2m =
			ROUND_DOWN(buddy_phy_start_addr, MIDDLE_PAGE_SIZE);
		arch_set_L2_entry_huge(buddy_start_round_down_2m,
							   KERNEL_PHY_TO_VIRT(buddy_start_round_down_2m),
							   &L2_table, (PT_DESC_V | PT_DESC_ATTR_LOWER_AF));
	}
}

void arch_init_pmm(struct setup_info *arch_setup_info) {

	struct fdt_header *dtb_header_ptr =
		(struct fdt_header *)(arch_setup_info->boot_dtb_header_base_addr);
	paddr buddy_phy_start = ROUND_UP(
		KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr), PAGE_SIZE);
	paddr kernel_phy_start = KERNEL_VIRT_TO_PHY((u64)(&_start));
	paddr kernel_phy_end = KERNEL_VIRT_TO_PHY((u64)(&_end));
	paddr buddy_phy_end = 0;
	u64 buddy_total_pages = 0;

	buddy_pmm.avaliable_phy_addr_end = 0;

	pr_info("start arch init pmm\n");
	for (u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap);
		 off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct);
		 off += sizeof(struct fdt_reserve_entry)) {
		struct fdt_reserve_entry *entry =
			(struct fdt_reserve_entry *)((u64)dtb_header_ptr + off);
		pr_info("reserve_entry: address 0x%x size: 0x%x\n", entry->address,
				entry->size);
	}
	buddy_pmm.m_regions->region_count = 0;
	arch_get_memory_regions(dtb_header_ptr, 0, 0);
	if (!buddy_pmm.m_regions->region_count)
		goto arch_init_pmm_error;

	calculate_avaliable_phy_addr_end();

	for (int order = 0; order <= BUDDY_MAXORDER; ++order)
		entry_per_bucket[order] = pages_per_bucket[order] = 0;
	calculate_bucket_space();
	for (int order = 0; order <= BUDDY_MAXORDER; ++order)
		buddy_total_pages += pages_per_bucket[order];
	buddy_phy_end = buddy_phy_start + buddy_total_pages * PAGE_SIZE;
	pr_info("buddy start 0x%x end 0x%x\n", buddy_phy_start, buddy_phy_end);

	// do some check

	arch_map_buddy_data_space(kernel_phy_start, kernel_phy_end, buddy_phy_start,
							  buddy_phy_end);

	generate_buddy_bucket(kernel_phy_start, kernel_phy_end, buddy_phy_start,
						  buddy_phy_end);

	return;
arch_init_pmm_error:
	arch_shutdown();
}
