/* x86_64 FADT consumption */
#ifndef _ARCH_X86_64_ACPI_ARCH_FADT_H_
#define _ARCH_X86_64_ACPI_ARCH_FADT_H_

#include <modules/acpi/acpi_fadt.h>
#include <rendezvos/error.h>

extern struct acpi_table_fadt *fadt_table;

/**
 * @brief Consume @c fadt_table .Stub for now.
 */
error_t arch_parser_fadt(void);

#endif
