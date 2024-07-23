#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/mm/page_table.h>
#include <arch/x86_64/mm/vmm.h>
#include <arch/x86_64/power_ctrl.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
#include <shampoos/limits.h>
#include <shampoos/mm/buddy_pmm.h>
extern char _start, _end; /*the kernel end virt addr*/
extern u64 L0_table, L1_table, L2_table;
extern struct buddy buddy_pmm;

extern u64 entry_per_bucket[BUDDY_MAXORDER + 1],
	pages_per_bucket[BUDDY_MAXORDER + 1];

static error_t arch_get_memory_regions(struct setup_info *arch_setup_info) {
	struct multiboot_info *mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
	struct multiboot_mmap_entry *mmap;
	vaddr add_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
	u64 length = mtb_info->mmap.mmap_length;

	/* check the multiboot header */
	if (!MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags, MULTIBOOT_INFO_FLAG_MEM) ||
		!MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags, MULTIBOOT_INFO_FLAG_MMAP)) {
		pr_info("no mem info\n");
		goto arch_init_pmm_error;
	}
	/*generate the memory region info*/
	buddy_pmm.m_regions->region_count = 0;
	for (mmap = (struct multiboot_mmap_entry *)add_ptr;
		 ((vaddr)mmap) < (add_ptr + length);
		 mmap = (struct multiboot_mmap_entry *)((vaddr)mmap + mmap->size +
												sizeof(mmap->size))) {
		if (mmap->addr + mmap->len > BIOS_MEM_UPPER &&
			mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			if (buddy_pmm.m_regions->memory_regions_insert(mmap->addr,
														   mmap->len)) {
				pr_error("we cannot manager toooo many memory regions\n");
				goto arch_init_pmm_error;
			}
		}
	}
	return 0;
arch_init_pmm_error:
	return -ENOMEM;
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
							   &L2_table, (PDE_P | PDE_RW | PDE_G | PDE_PS));
	}
}
void arch_init_pmm(struct setup_info *arch_setup_info) {

	u64 buddy_total_pages = 0;

	paddr kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
	paddr kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
	paddr buddy_phy_start = ROUND_UP(kernel_phy_end, PAGE_SIZE);
	paddr buddy_phy_end = 0;
	int kernel_region = -1;

	if (arch_get_memory_regions(arch_setup_info) < 0)
		goto arch_init_pmm_error;
	// adjust the memory regions, according to the kernel

	for (int i = 0; i < buddy_pmm.m_regions->region_count; i++) {
		if (buddy_pmm.m_regions->memory_regions_entry_empty(i))
			continue;
		struct region *reg = &buddy_pmm.m_regions->memory_regions[i];
		paddr region_end = reg->addr + reg->len;
		// find the region
		if (kernel_phy_start >= reg->addr && kernel_phy_end <= region_end) {
			// the kernel used all the memeory
			if (kernel_phy_start == reg->addr && kernel_phy_end == region_end)
				buddy_pmm.m_regions->memory_regions_delete(i);
			// only one size is used, just change the region
			else if (kernel_phy_start == reg->addr) {
				reg->addr = kernel_phy_end;
			} else if (kernel_phy_end == region_end) {
				reg->len = kernel_phy_start - reg->addr;
			} else {
				// both side have space, adjust the region and insert a new one
				buddy_pmm.m_regions->memory_regions_insert(
					reg->addr, kernel_phy_start - reg->addr);
				reg->addr = kernel_phy_end;
				reg->len = region_end - kernel_phy_end;
			}
			kernel_region = i;
		}
	}
	/*You need to check whether the kernel have been loaded all successfully*/
	if (kernel_region == -1) {
		pr_info("cannot load kernel\n");
		goto arch_init_pmm_error;
	}

	/* calculate the end of the buddy_pmm avaliable_phy_addr */
	calculate_avaliable_phy_addr_end();

	/*calculate how mach the buddy data need*/
	for (int order = 0; order <= BUDDY_MAXORDER; ++order)
		entry_per_bucket[order] = pages_per_bucket[order] = 0;
	calculate_bucket_space();
	for (int order = 0; order <= BUDDY_MAXORDER; ++order)
		buddy_total_pages += pages_per_bucket[order];
	buddy_phy_end = buddy_phy_start + buddy_total_pages * PAGE_SIZE;
	pr_info("buddy start 0x%x end 0x%x\n", buddy_phy_start, buddy_phy_end);

	if (ROUND_DOWN(buddy_phy_end, HUGE_PAGE_SIZE) !=
		ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE)) {
		pr_error("cannot load the buddy\n");
		goto arch_init_pmm_error;
	}
	if (buddy_pmm.m_regions->memory_regions[kernel_region].addr +
			buddy_pmm.m_regions->memory_regions[kernel_region].len <
		buddy_phy_end) {
		pr_error("cannot load the buddy\n");
		goto arch_init_pmm_error;
	}

	arch_map_buddy_data_space(kernel_phy_start, kernel_phy_end, buddy_phy_start,
							  buddy_phy_end);

	generate_buddy_bucket(kernel_phy_start, kernel_phy_end, buddy_phy_start,
						  buddy_phy_end);

	return;
arch_init_pmm_error:
	arch_shutdown();
}
