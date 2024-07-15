#include <arch/aarch64/boot/arch_setup.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <common/endianness.h>
#include <common/mm.h>
#include <modules/dtb/dtb.h>
#include <modules/dtb/print_property.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
extern u64 L2_table;
static void map_dtb(struct setup_info *arch_setup_info) {
	/*
		map the dtb, using the linux boot protocol, which define that:
		the dtb must be 8 byte align, and less then 2m
		as it haven't defined that dtb must 2m align, we must alloc 4m
	*/
	u64 vaddr = ROUND_UP(arch_setup_info->map_end_virt_addr, MIDDLE_PAGE_SIZE);
	u64 paddr = ROUND_DOWN(arch_setup_info->dtb_ptr, MIDDLE_PAGE_SIZE);
	arch_set_L2_entry_huge(paddr, vaddr, &L2_table,
						   (PT_DESC_V | PT_DESC_ATTR_LOWER_AF));
	vaddr += MIDDLE_PAGE_SIZE;
	paddr += MIDDLE_PAGE_SIZE;
	arch_set_L2_entry_huge(paddr, vaddr, &L2_table,
						   (PT_DESC_V | PT_DESC_ATTR_LOWER_AF));
	u64 offset = vaddr - paddr;
	arch_setup_info->boot_dtb_header_base_addr =
		arch_setup_info->dtb_ptr + offset;
	arch_setup_info->map_end_virt_addr =
		arch_setup_info->boot_dtb_header_base_addr + MIDDLE_PAGE_SIZE * 2;
}

int start_arch(struct setup_info *arch_setup_info) {
	map_dtb(arch_setup_info);
	struct fdt_header *dtb_header_ptr =
		(struct fdt_header *)(arch_setup_info->boot_dtb_header_base_addr);
	if (fdt_check_header(dtb_header_ptr)) {
		pr_info("check fdt header fail\n");
		goto start_arch_error;
	}
	// parse_print_dtb(dtb_header_ptr,0,0);

	return 0;
start_arch_error:
	return -EPERM;
}