/*
 *	It's a realization of ACPI Specification for version 6.5
 *	And I rewrite it , which is not a copy of linux acpi files
 */
#ifndef _ACPI_H_
#define _ACPI_H_

#include <modules/acpi/acpi_table.h>

struct acpi_table_rsdp* acpi_probe_rsdp(vaddr search_start_vaddr);
#endif
