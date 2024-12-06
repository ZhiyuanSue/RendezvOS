/*
 *	It's a realization of ACPI Specification for version 6.5
 *	And I rewrite it , which is not a copy of linux acpi files
 */
#ifndef _ACPI_H_
#define _ACPI_H_

#include "acpi_table.h"
#include "acpi_fadt.h"
#include "acpi_madt.h"
#include <common/stdbool.h>

struct acpi_table_rsdp* acpi_probe_rsdp(vaddr search_start_vaddr);
bool acpi_table_sig_check(char* acpi_table_sig_ptr, char* sig_chars);
#endif
