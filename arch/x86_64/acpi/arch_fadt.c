#include <arch/x86_64/acpi/arch_fadt.h>
#include <rendezvos/error.h>

struct acpi_table_fadt *fadt_table;

error_t arch_parser_fadt(void)
{
        /* TODO: SCI_INT, SMI_CMD / ACPI enable, RESET_REG, IAPC_BOOT_ARCH, … */
        (void)fadt_table;
        return REND_SUCCESS;
}
