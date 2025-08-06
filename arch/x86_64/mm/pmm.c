#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/mm/vmm.h>
#include <arch/x86_64/power_ctrl.h>
#include <modules/log/log.h>
#include <modules/acpi/acpi.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>

extern char _start, _end; /*the kernel end virt addr*/
extern u64 L2_table, L1_table;
extern struct memory_regions m_regions;

#define multiboot_insert_memory_region(mmap)                                    \
        if (mmap->addr + mmap->len > BIOS_MEM_UPPER                             \
            && mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {                      \
                if (m_regions.memory_regions_insert(mmap->addr, mmap->len)) {   \
                        print("we cannot manager toooo many memory regions\n"); \
                        goto arch_init_pmm_error;                               \
                } else {                                                        \
                        print("[ Phy_Mem\t@\t< 0x%x , 0x%x >]\n",               \
                              mmap->addr,                                       \
                              mmap->len);                                       \
                }                                                               \
        }

static error_t arch_get_memory_regions(struct setup_info *arch_setup_info)
{
        u32 mtb_magic;
        struct multiboot_info *mtb_info;

        struct multiboot2_info *mtb2_info;

        vaddr addr_ptr;
        /*some ptr is phisical, we need to change it to virtual*/

        mtb_magic = arch_setup_info->multiboot_magic;
        if (mtb_magic == MULTIBOOT_MAGIC) {
                /*multiboot 1 memory region detect*/
                mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
                addr_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
                u64 length = mtb_info->mmap.mmap_length;
                /* check the multiboot header */
                if (!MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                               MULTIBOOT_INFO_FLAG_MEM)
                    || !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                                  MULTIBOOT_INFO_FLAG_MMAP)) {
                        print("no mem info\n");
                        goto arch_init_pmm_error;
                }
                /*generate the memory region info*/
                m_regions.memory_regions_init(&m_regions);

                for_each_multiboot_mmap(addr_ptr, length)
                {
                        multiboot_insert_memory_region(mmap);
                }
        } else if (mtb_magic == MULTIBOOT2_MAGIC) {
                mtb2_info = GET_MULTIBOOT2_INFO(arch_setup_info);
                for_each_tag(mtb2_info)
                {
                        switch (tag->type) {
                        case MULTIBOOT2_TAG_TYPE_MMAP: {
                                addr_ptr =
                                        (vaddr)(((struct multiboot2_tag_mmap *)
                                                         tag)
                                                        ->entries);
                                for_each_multiboot2_mmap(tag, addr_ptr)
                                {
                                        multiboot_insert_memory_region(mmap);
                                }
                        } break;
                        }
                }
        }
        return (0);
arch_init_pmm_error:
        return (-E_RENDEZVOS);
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
void reserve_arch_region(struct setup_info *arch_setup_info)
{
        // reserve acpi region
        struct acpi_table_rsdp *rsdp_table =
                acpi_probe_rsdp(KERNEL_VIRT_OFFSET);
        if (!rsdp_table) {
                print("not find any rsdp\n");
                return;
        }
        // print("====== rsdp @[0x%x]] ======\n", rsdp_table);
        arch_setup_info->rsdp_addr = (vaddr)rsdp_table;
        if (rsdp_table->revision == 0) {
                // print("====== rsdt @[0x%x]] ======\n",
                // rsdp_table->rsdt_address);
                paddr acpi_reserve_phy_start =
                        ROUND_DOWN(rsdp_table->rsdt_address, MIDDLE_PAGE_SIZE);
                paddr acpi_reserve_phy_end =
                        acpi_reserve_phy_start + MIDDLE_PAGE_SIZE;
                int acpi_region = m_regions.memory_regions_reserve_region(
                        acpi_reserve_phy_start, acpi_reserve_phy_end);
                print("[ ACPI_DATA\t@\t< 0x%x , 0x%x >]\n",
                      KERNEL_PHY_TO_VIRT(acpi_reserve_phy_start),
                      KERNEL_PHY_TO_VIRT(acpi_reserve_phy_end));
                if (acpi_region == -1) {
                        print("cannot load acpi\n");
                        return;
                }
        } else {
                print("[ ACPI ] unsupported vision: %d\n",
                      rsdp_table->revision);
        }
}
void arch_init_pmm(struct setup_info *arch_setup_info)
{
        paddr kernel_phy_start;
        paddr kernel_phy_end;
        paddr per_cpu_phy_start, per_cpu_phy_end;
        paddr pmm_data_phy_start;
        paddr pmm_data_phy_end;
        int kernel_region;

        kernel_phy_start = KERNEL_VIRT_TO_PHY((vaddr)(&_start));
        kernel_phy_end = KERNEL_VIRT_TO_PHY((vaddr)(&_end));
        per_cpu_phy_start = per_cpu_phy_end = kernel_phy_end;
        reserve_per_cpu_region(&per_cpu_phy_end);
        pmm_data_phy_end = pmm_data_phy_start =
                ROUND_UP(per_cpu_phy_end, PAGE_SIZE);
        if (arch_get_memory_regions(arch_setup_info) < 0)
                goto arch_init_pmm_error;
        // adjust the memory regions, according to the kernel
        kernel_region = m_regions.memory_regions_reserve_region(
                kernel_phy_start, kernel_phy_end);

        print("[ KERNEL_REGION\t@\t< 0x%x , 0x%x >]\n",
              (vaddr)(&_start),
              (vaddr)(&_end));
        print("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
              KERNEL_PHY_TO_VIRT(per_cpu_phy_start),
              KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        reserve_arch_region(arch_setup_info);
        /*You need to check whether the kernel have been loaded all
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
                print("cannot load the pmm data\n");
                goto arch_init_pmm_error;
        }
        arch_map_extra_data_space(kernel_phy_start,
                                  kernel_phy_end,
                                  per_cpu_phy_start,
                                  pmm_data_phy_end,
                                  pmm_data_phy_start,
                                  L2_table_pages);
        clean_per_cpu_region(per_cpu_phy_start);
        /*we should also do not clean the pmm l2 table region*/
        clean_pmm_region(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                         pmm_data_phy_end);
        generate_pmm_data(pmm_data_phy_start + L2_table_pages * PAGE_SIZE,
                          pmm_data_phy_end);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
