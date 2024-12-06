#include <modules/acpi/acpi.h>
#include <common/stddef.h>
#include <common/types.h>
#include <common/stdbool.h>
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