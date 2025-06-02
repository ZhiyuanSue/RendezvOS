#include <modules/acpi/acpi.h>
#include <common/stddef.h>
#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>
struct acpi_table_sig acpi_table_sigs[ACPI_NR_TABLES] = {
        {ACPI_APIC, ACPI_SIG_APIC}, {ACPI_BERT, ACPI_SIG_BERT},
        {ACPI_BGRT, ACPI_SIG_BGRT}, {ACPI_CCEL, ACPI_SIG_CCEL},
        {ACPI_CPEP, ACPI_SIG_CPEP}, {ACPI_DSDT, ACPI_SIG_DSDT},
        {ACPI_ECDT, ACPI_SIG_DSDT}, {ACPI_EINJ, ACPI_SIG_EINJ},
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
struct acpi_table_rsdp* acpi_probe_rsdp(vaddr search_start_vaddr)
{
        struct acpi_table_rsdp* rsdp = NULL;
        vaddr first_region_start = search_start_vaddr + 0x00080000;
        vaddr first_region_end = first_region_start + 0x400;
        u8 cmp_str[8] = ACPI_SIG_RSDP;

        vaddr second_region_start = search_start_vaddr + 0x000E0000;
        vaddr second_region_end = search_start_vaddr + 0x000FFFFF + 1;
        for (vaddr search_addr = first_region_start;
             search_addr < first_region_end;
             search_addr += 16) {
                bool find = true;
                for (int i = 0; i < 8; i++) {
                        if (((u8*)search_addr)[i] != cmp_str[i]) {
                                find = false;
                                break;
                        }
                }
                if (find) {
                        rsdp = (struct acpi_table_rsdp*)search_addr;
                        break;
                }
        }
        if (rsdp)
                return rsdp;
        for (vaddr search_addr = second_region_start;
             search_addr < second_region_end;
             search_addr += 16) {
                bool find = true;
                for (int i = 0; i < 8; i++) {
                        if (((u8*)search_addr)[i] != cmp_str[i]) {
                                find = false;
                                break;
                        }
                }
                if (find) {
                        rsdp = (struct acpi_table_rsdp*)search_addr;
                        break;
                }
        }
        return rsdp;
}
bool acpi_table_sig_check(char* acpi_table_sig_ptr, char* sig_chars)
{
        for (int i = 0; i < ACPI_SIG_LENG; i++) {
                if (acpi_table_sig_ptr[i] != sig_chars[i]) {
                        return false;
                }
        }
        return true;
}
enum acpi_table_sig_enum
get_acpi_table_type_from_sig(struct acpi_table_head* acpi_head)
{
        for (int i = 0; i < ACPI_NR_TABLES; i++) {
                if (acpi_table_sig_check(acpi_head->signature,
                                         acpi_table_sigs[i].sig_char))
                        return acpi_table_sigs[i].sig_enum;
        }
        return -E_RENDEZVOS;
}