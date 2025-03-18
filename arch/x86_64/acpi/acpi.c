#include <modules/acpi/acpi.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/percpu.h>
#include <rendezvos/error.h>
#include <modules/log/log.h>
#include <rendezvos/percpu.h>
extern int BSP_ID;
struct acpi_table_fadt *fadt_table;
extern struct acpi_table_madt *madt_table;
static inline error_t parser_facp()
{
        return 0;
}
static inline error_t parser_acpi_tables(enum acpi_table_sig_enum sig_enum,
                                         struct acpi_table_head *table_head)
{
        pr_info("acpi table type is %d\n", sig_enum);
        switch (sig_enum) {
        case ACPI_FACP:
                fadt_table = (struct acpi_table_fadt *)table_head;
                return parser_facp();
                break;
        case ACPI_APIC:
                madt_table = (struct acpi_table_madt *)table_head;
                return parser_apic();
                break;
        default:
                return -EPERM;
        }
}
error_t acpi_init(vaddr rsdp_addr)
{
        struct acpi_table_rsdp *rsdp_table =
                (struct acpi_table_rsdp *)(rsdp_addr);
        if (rsdp_table->revision == 0) {
                // we must use cpu 0 to map
                paddr rsdt_page =
                        ROUND_DOWN(rsdp_table->rsdt_address, MIDDLE_PAGE_SIZE);
                vaddr rsdt_map_page =
                        ROUND_DOWN(KERNEL_PHY_TO_VIRT(rsdp_table->rsdt_address),
                                   MIDDLE_PAGE_SIZE);
                if (!have_mapped(get_current_kernel_vspace_root(),
                                 VPN(rsdt_map_page),
                                 &per_cpu(Map_Handler, BSP_ID))) {
                        paddr vspace_root = get_current_kernel_vspace_root();
                        map(&vspace_root,
                            PPN(rsdt_page),
                            VPN(rsdt_map_page),
                            2,
                            PAGE_ENTRY_NONE,
                            &per_cpu(Map_Handler, BSP_ID));
                }
                struct acpi_table_rsdt *rsdt_table =
                        (struct acpi_table_rsdt *)KERNEL_PHY_TO_VIRT(
                                rsdp_table->rsdt_address);
                if (!acpi_table_sig_check(rsdt_table->signature,
                                          ACPI_SIG_RSDT)) {
                        pr_error("invalid signature of rsdt table\n");
                        return -EPERM;
                }

                int nr_rsdt_entry = (rsdt_table->length - ACPI_HEAD_SIZE)
                                    / ACPI_RSDT_ENTRY_SIZE;
                for (int i = 0; i < nr_rsdt_entry; i++) {
                        paddr entry_paddr = ((u32 *)&(rsdt_table->entry))[i];
                        vaddr entry_vaddr =
                                KERNEL_PHY_TO_VIRT((u64)entry_paddr);
                        struct acpi_table_head *tmp_table_head =
                                (struct acpi_table_head *)entry_vaddr;
                        enum acpi_table_sig_enum sig =
                                get_acpi_table_type_from_sig(tmp_table_head);
                        if (sig == -1) {
                                pr_error("undefined acpi table ");
                                for (int j = 0; j < ACPI_SIG_LENG; j++) {
                                        pr_error("%c",
                                                 tmp_table_head->signature[j]);
                                }
                                pr_error("\n");
                        } else {
                                parser_acpi_tables(sig, tmp_table_head);
                        }
                }
        } else {
                pr_error("[ ACPI ] unsupported vision: %d\n",
                         rsdp_table->revision);
                return -EPERM;
        }
        return 0;
}