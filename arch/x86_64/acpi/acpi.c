#include <modules/acpi/acpi.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/percpu.h>
extern u32 BSP_ID;
struct acpi_table_fadt *fadt_table;
extern struct acpi_table_madt *madt_table;
static inline error_t parser_facp(void)
{
        return REND_SUCCESS;
}
static inline error_t parser_acpi_tables(enum acpi_table_sig_enum sig_enum,
                                         struct acpi_table_head *table_head)
{
        print("acpi table type is %d\n", sig_enum);
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
                return -E_RENDEZVOS;
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
                if (!have_mapped(current_vspace,
                                 VPN(rsdt_map_page),
                                 &per_cpu(Map_Handler, BSP_ID))) {
                        map(current_vspace,
                            PPN(rsdt_page),
                            VPN(rsdt_map_page),
                            2,
                            PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                    | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE,
                            &per_cpu(Map_Handler, BSP_ID),
                            NULL);
                }
                struct acpi_table_rsdt *rsdt_table =
                        (struct acpi_table_rsdt *)KERNEL_PHY_TO_VIRT(
                                rsdp_table->rsdt_address);
                if (!acpi_table_sig_check(rsdt_table->signature,
                                          ACPI_SIG_RSDT)) {
                        print("invalid signature of rsdt table\n");
                        return -E_RENDEZVOS;
                }

                int nr_rsdt_entry = (rsdt_table->length - ACPI_HEAD_SIZE)
                                    / ACPI_RSDT_ENTRY_SIZE;
                for (int i = 0; i < nr_rsdt_entry; i++) {
                        paddr entry_paddr = ((u32 *)&(rsdt_table->entry))[i];
                        vaddr entry_vaddr =
                                KERNEL_PHY_TO_VIRT((u64)entry_paddr);
                        struct acpi_table_head *tmp_table_head =
                                (struct acpi_table_head *)entry_vaddr;
                        int sig = get_acpi_table_type_from_sig(tmp_table_head);
                        if (sig == -1) {
                                print("undefined acpi table ");
                                for (int j = 0; j < ACPI_SIG_LENG; j++) {
                                        print("%c",
                                              tmp_table_head->signature[j]);
                                }
                                print("\n");
                        } else {
                                parser_acpi_tables(sig, tmp_table_head);
                        }
                }
        } else {
                print("[ ACPI ] unsupported vision: %d\n",
                      rsdp_table->revision);
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;
}