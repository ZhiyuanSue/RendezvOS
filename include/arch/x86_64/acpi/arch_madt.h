/*
 * x86_64 MADT consumption (signature in RSDP table is still "APIC").
 */
#ifndef _ARCH_X86_64_ACPI_ARCH_MADT_H_
#define _ARCH_X86_64_ACPI_ARCH_MADT_H_

#include <modules/acpi/acpi_madt.h>
#include <rendezvos/error.h>

extern struct acpi_table_madt *madt_table;

/**
 * @brief Walk MADT records and parser it.
 */
error_t arch_parser_madt(void);

#endif
