#include <modules/acpi/acpi.h>
#include <modules/acpi/acpi_arch_hooks.h>
#include <modules/log/log.h>
#include <common/stddef.h>
#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/mm_kernel_utils.h>
#include <rendezvos/smp/percpu.h>

struct acpi_table_sig acpi_table_sigs[ACPI_NR_TABLES] = {
        {ACPI_APIC, ACPI_SIG_APIC}, {ACPI_BERT, ACPI_SIG_BERT},
        {ACPI_BGRT, ACPI_SIG_BGRT}, {ACPI_CCEL, ACPI_SIG_CCEL},
        {ACPI_CPEP, ACPI_SIG_CPEP}, {ACPI_DSDT, ACPI_SIG_DSDT},
        {ACPI_ECDT, ACPI_SIG_ECDT}, {ACPI_EINJ, ACPI_SIG_EINJ},
        {ACPI_ERST, ACPI_SIG_ERST}, {ACPI_FACP, ACPI_SIG_FACP},
        {ACPI_FACS, ACPI_SIG_FACS}, {ACPI_FPDT, ACPI_SIG_FPDT},
        {ACPI_GTDT, ACPI_SIG_GTDT}, {ACPI_HEST, ACPI_SIG_HEST},
        {ACPI_MISC, ACPI_SIG_MISC}, {ACPI_MSCT, ACPI_SIG_MSCT},
        {ACPI_MPST, ACPI_SIG_MPST}, {ACPI_NFIT, ACPI_SIG_NFIT},
        {ACPI_OEMx, ACPI_SIG_OEMx}, {ACPI_PCCT, ACPI_SIG_PCCT},
        {ACPI_PHAT, ACPI_SIG_PHAT}, {ACPI_PMTT, ACPI_SIG_PMTT},
        {ACPI_PPTT, ACPI_SIG_PPTT}, {ACPI_PSDT, ACPI_SIG_PSDT},
        {ACPI_RASF, ACPI_SIG_RASF}, {ACPI_RAS2, ACPI_SIG_RAS2},
        {ACPI_RSDT, ACPI_SIG_RSDT}, {ACPI_SBST, ACPI_SIG_SBST},
        {ACPI_SDEV, ACPI_SIG_SDEV}, {ACPI_SLIT, ACPI_SIG_SLIT},
        {ACPI_SRAT, ACPI_SIG_SRAT}, {ACPI_SSDT, ACPI_SIG_SSDT},
        {ACPI_SVKL, ACPI_SIG_SVKL}, {ACPI_XSDT, ACPI_SIG_XSDT},
};

/**
 * IA-PC legacy BIOS RSDP search (ACPI "Finding the RSDP on IA-PC Systems").
 * Ensures the two physical windows via identity map, then scans at
 * KERNEL_PHY_TO_VIRT — same VA contract as ensure_kernel_identity_range.
 */
static error_t acpi_probe_rsdp_legacy_bios(struct acpi_table_rsdp **rsdp_out)
{
        const ENTRY_FLAGS_t probe_flags = PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE;
        struct VSpace *vs = per_cpu(current_vspace, BSP_ID);
        struct map_handler *handler = &per_cpu(Map_Handler, BSP_ID);
        error_t error;
        struct acpi_table_rsdp *rsdp = NULL;
        u8 cmp_str[8] = ACPI_SIG_RSDP;

        error = ensure_kernel_identity_range(vs,
                                             ACPI_RSDP_EBDA_PHY,
                                             ACPI_RSDP_EBDA_LEN,
                                             probe_flags,
                                             handler);
        if (error != REND_SUCCESS)
                return error;
        error = ensure_kernel_identity_range(
                vs, ACPI_RSDP_ROM_PHY, ACPI_RSDP_ROM_LEN, probe_flags, handler);
        if (error != REND_SUCCESS)
                return error;

        for (vaddr search_addr = KERNEL_PHY_TO_VIRT(ACPI_RSDP_EBDA_PHY);
             search_addr
             < KERNEL_PHY_TO_VIRT(ACPI_RSDP_EBDA_PHY) + ACPI_RSDP_EBDA_LEN;
             search_addr += 16) {
                bool find = true;
                for (int i = 0; i < 8; i++) {
                        if (((u8 *)search_addr)[i] != cmp_str[i]) {
                                find = false;
                                break;
                        }
                }
                if (find) {
                        rsdp = (struct acpi_table_rsdp *)search_addr;
                        break;
                }
        }
        if (!rsdp) {
                for (vaddr search_addr = KERNEL_PHY_TO_VIRT(ACPI_RSDP_ROM_PHY);
                     search_addr < KERNEL_PHY_TO_VIRT(ACPI_RSDP_ROM_PHY)
                                           + ACPI_RSDP_ROM_LEN;
                     search_addr += 16) {
                        bool find = true;
                        for (int i = 0; i < 8; i++) {
                                if (((u8 *)search_addr)[i] != cmp_str[i]) {
                                        find = false;
                                        break;
                                }
                        }
                        if (find) {
                                rsdp = (struct acpi_table_rsdp *)search_addr;
                                break;
                        }
                }
        }

        if (!rsdp)
                return -E_RENDEZVOS;
        *rsdp_out = rsdp;
        return REND_SUCCESS;
}

error_t acpi_probe_rsdp(struct acpi_table_rsdp **rsdp_out)
{
        /*
         * Today only legacy BIOS windows (ensure + scan). UEFI would be a
         * separate branch that does not map these windows.
         */
        return acpi_probe_rsdp_legacy_bios(rsdp_out);
}

bool acpi_table_sig_check(char *acpi_table_sig_ptr, char *sig_chars)
{
        for (int i = 0; i < ACPI_SIG_LENG; i++) {
                if (acpi_table_sig_ptr[i] != sig_chars[i]) {
                        return false;
                }
        }
        return true;
}
enum acpi_table_sig_enum
get_acpi_table_type_from_sig(struct acpi_table_head *acpi_head)
{
        for (int i = 0; i < ACPI_NR_TABLES; i++) {
                if (acpi_table_sig_check(acpi_head->signature,
                                         acpi_table_sigs[i].sig_char))
                        return acpi_table_sigs[i].sig_enum;
        }
        return -E_RENDEZVOS;
}

static inline error_t parser_acpi_tables(enum acpi_table_sig_enum sig_enum,
                                         struct acpi_table_head *table_head)
{
        print("acpi table type is %d\n", sig_enum);
        switch (sig_enum) {
        case ACPI_FACP: /* signature "FACP" → FADT */
                fadt_table = (struct acpi_table_fadt *)table_head;
                return arch_parser_fadt();
        case ACPI_APIC: /* signature "APIC" → MADT */
                madt_table = (struct acpi_table_madt *)table_head;
                return arch_parser_madt();
        default:
                return -E_RENDEZVOS;
        }
}

error_t acpi_init(struct setup_info *arch_setup_info)
{
        const ENTRY_FLAGS_t table_flags = PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                                          | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE;
        struct VSpace *vs = per_cpu(current_vspace, BSP_ID);
        struct map_handler *handler = &per_cpu(Map_Handler, BSP_ID);
        struct acpi_table_rsdp *rsdp_table = NULL;
        error_t error;

        /* UEFI may read an RSDP hint from setup_info; legacy BIOS ignores it.
         */
        (void)arch_setup_info;

        error = acpi_probe_rsdp(&rsdp_table);
        if (error != REND_SUCCESS) {
                print("[ ACPI ] ERROR: RSDP probe failed\n");
                return error;
        }

        if (rsdp_table->revision == 0) {
                error = ensure_kernel_identity_range(vs,
                                                     rsdp_table->rsdt_address,
                                                     ACPI_HEAD_SIZE,
                                                     table_flags,
                                                     handler);
                if (error != REND_SUCCESS)
                        return error;

                struct acpi_table_rsdt *rsdt_table =
                        (struct acpi_table_rsdt *)KERNEL_PHY_TO_VIRT(
                                rsdp_table->rsdt_address);
                if (!acpi_table_sig_check(rsdt_table->signature,
                                          ACPI_SIG_RSDT)) {
                        print("[ ACPI ] ERROR: invalid RSDT signature\n");
                        return -E_RENDEZVOS;
                }

                error = ensure_kernel_identity_range(vs,
                                                     rsdp_table->rsdt_address,
                                                     rsdt_table->length,
                                                     table_flags,
                                                     handler);
                if (error != REND_SUCCESS)
                        return error;

                int nr_rsdt_entry = (rsdt_table->length - ACPI_HEAD_SIZE)
                                    / ACPI_RSDT_ENTRY_SIZE;
                for (int i = 0; i < nr_rsdt_entry; i++) {
                        paddr entry_paddr = ((u32 *)&(rsdt_table->entry))[i];

                        error = ensure_kernel_identity_range(vs,
                                                             entry_paddr,
                                                             ACPI_HEAD_SIZE,
                                                             table_flags,
                                                             handler);
                        if (error != REND_SUCCESS)
                                return error;

                        struct acpi_table_head *table_head =
                                (struct acpi_table_head *)KERNEL_PHY_TO_VIRT(
                                        (u64)entry_paddr);

                        error = ensure_kernel_identity_range(vs,
                                                             entry_paddr,
                                                             table_head->length,
                                                             table_flags,
                                                             handler);
                        if (error != REND_SUCCESS)
                                return error;

                        int sig = get_acpi_table_type_from_sig(table_head);
                        if (sig == -E_RENDEZVOS) {
                                print("[ ACPI ] ERROR: undefined table ");
                                for (int j = 0; j < ACPI_SIG_LENG; j++)
                                        print("%c", table_head->signature[j]);
                                print("\n");
                                continue;
                        }

                        error = parser_acpi_tables(
                                (enum acpi_table_sig_enum)sig, table_head);
                        if (error != REND_SUCCESS) {
                                print("[ ACPI ] ERROR: parser failed sig enum %d\n",
                                      sig);
                                return error;
                        }
                }
        } else {
                print("[ ACPI ] ERROR: unsupported revision %d\n",
                      rsdp_table->revision);
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;
}
