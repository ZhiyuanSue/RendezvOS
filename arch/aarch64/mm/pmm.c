#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/pmm.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/endianness.h>
#include <modules/dtb/dtb.h>
#include <shampoos/limits.h>
#include <shampoos/mm/pmm.h>

extern char _start, _end; /*the kernel end virt addr*/
extern u64 L0_table, L1_table, L2_table;
extern struct memory_regions	m_regions;

extern struct property_type		property_types[PROPERTY_TYPE_NUM];
static void	arch_get_memory_regions(void *fdt, int offset, int depth)
{
	const char			*device_type_str = property_types[PROPERTY_TYPE_DEVICE_TYPE].property_string;
	const char			*memory_str = "memory\0";
	const char			*reg_str = property_types[PROPERTY_TYPE_REG].property_string;
	struct fdt_property	*prop;
	const char			*property_name;
	const char			*data;
	const char			*s;
	u_int32_t			len;
	u32					*u32_data;

	int property, node;
	/*
		actually we seems to use something like of_find_node_by_type
		but now we have no memory to alloc any struct of device_node
	*/
	fdt_for_each_property_offset(property, fdt, offset)
	{
		prop = (struct fdt_property *)fdt_offset_ptr(fdt, property,
				FDT_TAGSIZE);
		property_name = fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
		data = (const char *)(prop->data);
		if (!strcmp(property_name, device_type_str) && !strcmp(data,
				memory_str))
		{
			goto find_memory_node;
		}
	}
	fdt_for_each_subnode(node, fdt, offset)
	{
		arch_get_memory_regions(fdt, node, depth + 1);
	}
	return ;
find_memory_node:
	fdt_for_each_property_offset(property, fdt, offset)
	{
		prop = (struct fdt_property *)fdt_offset_ptr(fdt, property,
				FDT_TAGSIZE);
		s = fdt_string(fdt, SWAP_ENDIANNESS_32(prop->nameoff));
		data = (const char *)(prop->data);
		len = SWAP_ENDIANNESS_32(prop->len);
		if (!strcmp(s, reg_str))
		{
			u32_data = (u32 *)data;
			for (int index = 0; index < len; index += sizeof(u32) * 4)
			{
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
				m_regions.memory_regions_insert(addr, len);
			}
		}
	}
}

static void	arch_map_pmm_data_space(paddr kernel_phy_start,
		paddr kernel_phy_end, paddr pmm_data_phy_start, paddr pmm_data_phy_end)
{
	paddr	pmm_data_phy_start_addr;
	paddr	kernel_end_phy_addr_round_up;
	paddr	pmm_data_start_round_down_2m;

	pmm_data_phy_start_addr = pmm_data_phy_start;
	kernel_end_phy_addr_round_up = ROUND_UP(kernel_phy_end, MIDDLE_PAGE_SIZE);
	if (pmm_data_phy_start_addr < kernel_end_phy_addr_round_up)
		pmm_data_phy_start_addr = kernel_end_phy_addr_round_up;
	/*for we have mapped the 2m align
															space of kernel*/
	for (; pmm_data_phy_start_addr < pmm_data_phy_end; pmm_data_phy_start_addr
		+= MIDDLE_PAGE_SIZE)
	{
		/*As pmm and vmm part is not usable now, we still use boot page table*/
		pmm_data_start_round_down_2m = ROUND_DOWN(pmm_data_phy_start_addr,
				MIDDLE_PAGE_SIZE);
		arch_set_L2_entry_huge(pmm_data_start_round_down_2m,
			KERNEL_PHY_TO_VIRT(pmm_data_start_round_down_2m),
			(union L2_entry_huge *)&L2_table,
			(PT_DESC_V | PT_DESC_ATTR_LOWER_AF));
	}
}

void	arch_init_pmm(struct setup_info *arch_setup_info)
{
	struct fdt_header			*dtb_header_ptr;
	paddr						pmm_data_phy_start;
	paddr						kernel_phy_start;
	paddr						kernel_phy_end;
	paddr						pmm_data_phy_end;
	int							kernel_region;
	struct fdt_reserve_entry	*entry;
	struct region				*reg;
	paddr						region_end;
	paddr						map_end_phy_addr;

	dtb_header_ptr = (struct fdt_header *)(arch_setup_info->boot_dtb_header_base_addr);
	pmm_data_phy_start = ROUND_UP(KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr),
			PAGE_SIZE);
	kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
	kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
	pmm_data_phy_end = 0;
	kernel_region = -1;
	pr_info("start arch init pmm\n");
	for (u64 off = SWAP_ENDIANNESS_32(dtb_header_ptr->off_mem_rsvmap); off < SWAP_ENDIANNESS_32(dtb_header_ptr->off_dt_struct); off
		+= sizeof(struct fdt_reserve_entry))
	{
		entry = (struct fdt_reserve_entry *)((u64)dtb_header_ptr + off);
		pr_info("reserve_entry: address 0x%x size: 0x%x\n", entry->address,
			entry->size);
	}
	m_regions.region_count = 0;
	arch_get_memory_regions(dtb_header_ptr, 0, 0);
	if (!m_regions.region_count)
		goto arch_init_pmm_error;
	// adjust the memory regions, according to the kernel
	for (int i = 0; i < m_regions.region_count; i++)
	{
		if (m_regions.memory_regions_entry_empty(i))
			continue ;
		reg = &m_regions.memory_regions[i];
		region_end = reg->addr + reg->len;
		map_end_phy_addr = KERNEL_VIRT_TO_PHY(arch_setup_info->map_end_virt_addr);
		// find the region
		if (kernel_phy_start >= reg->addr && map_end_phy_addr <= region_end)
		{
			// the kernel used all the memeory
			if (kernel_phy_start == reg->addr && map_end_phy_addr == region_end)
				m_regions.memory_regions_delete(i);
			// only one size is used, just change the region
			else if (kernel_phy_start == reg->addr)
			{
				reg->addr = map_end_phy_addr;
			}
			else if (map_end_phy_addr == region_end)
			{
				reg->len = kernel_phy_start - reg->addr;
			}
			else
			{
				// both side have space, adjust the region and insert a new one
				m_regions.memory_regions_insert(reg->addr, kernel_phy_start
					- reg->addr);
				reg->addr = map_end_phy_addr;
				reg->len = region_end - map_end_phy_addr;
			}
			kernel_region = i;
		}
	}
	/*You need to check whether the kernel and dtb have been loaded all
		* successfully*/
	if (kernel_region == -1)
	{
		pr_info("cannot load kernel\n");
		goto arch_init_pmm_error;
	}
	pmm_data_phy_end = pmm_data_phy_start + calculate_pmm_space() * PAGE_SIZE;
	pr_info("pmm_data start 0x%x end 0x%x\n", pmm_data_phy_start,
		pmm_data_phy_end);
	if (ROUND_DOWN(pmm_data_phy_end,
			HUGE_PAGE_SIZE) != ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE))
	{
		pr_error("cannot load the pmm data\n");
		goto arch_init_pmm_error;
	}
	if (m_regions.memory_regions[kernel_region].addr
		+ m_regions.memory_regions[kernel_region].len < pmm_data_phy_end)
	{
		pr_error("cannot load the pmm_data\n");
		goto arch_init_pmm_error;
	}
	arch_map_pmm_data_space(kernel_phy_start, kernel_phy_end,
		pmm_data_phy_start, pmm_data_phy_end);
	generate_pmm_data(kernel_phy_start, kernel_phy_end, pmm_data_phy_start,
		pmm_data_phy_end);
	return ;
arch_init_pmm_error:
	arch_shutdown();
}
