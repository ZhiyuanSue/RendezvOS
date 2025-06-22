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
extern u64 L2_table;
extern struct memory_regions m_regions;

#define multiboot_insert_memory_region(mmap)                                      \
        if (mmap->addr + mmap->len > BIOS_MEM_UPPER                               \
            && mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {                        \
                if (m_regions.memory_regions_insert(mmap->addr, mmap->len)) {     \
                        pr_error(                                                 \
                                "we cannot manager toooo many memory regions\n"); \
                        goto arch_init_pmm_error;                                 \
                } else {                                                          \
                        pr_info("[ Phy_Mem\t@\t< 0x%x , 0x%x >]\n",               \
                                mmap->addr,                                       \
                                mmap->len);                                       \
                }                                                                 \
        }

static error_t arch_get_memory_regions(struct setup_info *arch_setup_info)
{
        u32 mtb_magic;
        struct multiboot_info *mtb_info;

        struct multiboot2_info *mtb2_info;

        vaddr add_ptr;
        u64 length;

        mtb_magic = arch_setup_info->multiboot_magic;
        if (mtb_magic == MULTIBOOT_MAGIC) {
                /*multiboot 1 memory region detect*/
                mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
                add_ptr = mtb_info->mmap.mmap_addr + KERNEL_VIRT_OFFSET;
                length = mtb_info->mmap.mmap_length;
                /* check the multiboot header */
                if (!MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                               MULTIBOOT_INFO_FLAG_MEM)
                    || !MULTIBOOT_INFO_FLAG_CHECK(mtb_info->flags,
                                                  MULTIBOOT_INFO_FLAG_MMAP)) {
                        pr_info("no mem info\n");
                        goto arch_init_pmm_error;
                }
                /*generate the memory region info*/
                m_regions.memory_regions_init(&m_regions);

                for_each_multiboot_mmap(add_ptr, length)
                {
                        multiboot_insert_memory_region(mmap);
                }
        } else if (mtb_magic == MULTIBOOT2_MAGIC) {
                mtb2_info = GET_MULTIBOOT2_INFO(arch_setup_info);
                for_each_tag(mtb2_info)
                {
                        switch (tag->type) {
                        case MULTIBOOT2_TAG_TYPE_MMAP: {
                                for_each_multiboot2_mmap(tag)
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
void reserve_arch_region(struct setup_info *arch_setup_info)
{
        // reserve acpi region
        struct acpi_table_rsdp *rsdp_table =
                acpi_probe_rsdp(KERNEL_VIRT_OFFSET);
        if (!rsdp_table) {
                pr_info("not find any rsdp\n");
                return;
        }
        // pr_info("====== rsdp @[0x%x]] ======\n", rsdp_table);
        arch_setup_info->rsdp_addr = (vaddr)rsdp_table;
        if (rsdp_table->revision == 0) {
                // pr_info("====== rsdt @[0x%x]] ======\n",
                // rsdp_table->rsdt_address);
                paddr acpi_reserve_phy_start =
                        ROUND_DOWN(rsdp_table->rsdt_address, MIDDLE_PAGE_SIZE);
                paddr acpi_reserve_phy_end =
                        acpi_reserve_phy_start + MIDDLE_PAGE_SIZE;
                int acpi_region = m_regions.memory_regions_reserve_region(
                        acpi_reserve_phy_start, acpi_reserve_phy_end);
                pr_info("[ ACPI_DATA\t@\t< 0x%x , 0x%x >]\n",
                        KERNEL_PHY_TO_VIRT(acpi_reserve_phy_start),
                        KERNEL_PHY_TO_VIRT(acpi_reserve_phy_end));
                if (acpi_region == -1) {
                        pr_info("cannot load acpi\n");
                        return;
                }
        } else {
                pr_error("[ ACPI ] unsupported vision: %d\n",
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

        pr_info("[ KERNEL_REGION\t@\t< 0x%x , 0x%x >]\n",
                (vaddr)(&_start),
                (vaddr)(&_end));
        pr_info("[ PERCPU_REGION\t@\t< 0x%x , 0x%x >]\n",
                KERNEL_PHY_TO_VIRT(per_cpu_phy_start),
                KERNEL_PHY_TO_VIRT(per_cpu_phy_end));

        reserve_arch_region(arch_setup_info);
        /*You need to check whether the kernel have been loaded all
         * successfully*/
        if (kernel_region == -1) {
                pr_info("cannot load kernel\n");
                goto arch_init_pmm_error;
        }
        pmm_data_phy_end += calculate_pmm_space() * PAGE_SIZE;
        pr_info("[ PMM_DATA\t@\t< 0x%x , 0x%x >]\n",
                KERNEL_PHY_TO_VIRT(pmm_data_phy_start),
                KERNEL_PHY_TO_VIRT(pmm_data_phy_end));
        if (ROUND_DOWN(pmm_data_phy_end, HUGE_PAGE_SIZE)
            != ROUND_DOWN(kernel_phy_start, HUGE_PAGE_SIZE)) {
                pr_error("cannot load the pmm data\n");
                goto arch_init_pmm_error;
        }
        if (m_regions.memory_regions[kernel_region].addr
                    + m_regions.memory_regions[kernel_region].len
            < pmm_data_phy_end) {
                pr_error("cannot load the pmm data\n");
                goto arch_init_pmm_error;
        }
        arch_map_extra_data_space(kernel_phy_start,
                                  kernel_phy_end,
                                  per_cpu_phy_start,
                                  pmm_data_phy_end);
        clean_per_cpu_region(per_cpu_phy_start);
        clean_pmm_region(pmm_data_phy_start, pmm_data_phy_end);
        generate_pmm_data(pmm_data_phy_start, pmm_data_phy_end);
        return;
arch_init_pmm_error:
        arch_shutdown();
}
