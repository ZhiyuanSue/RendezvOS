/*
 * Arch-provided ACPI consumers (@c arch_* naming) — module-side contract.
 *
 * @c acpi_init walks RSDT and dispatches by *signature*; handlers are named
 * after the *table* (FADT / MADT):
 *   signature "FACP" → FADT → @c arch_parser_fadt  (arch/.../acpi/arch_fadt.*)
 *   signature "APIC" → MADT → @c arch_parser_madt  (arch/.../acpi/arch_madt.*)
 *
 * Arch code and x86 consumers should include @c arch_fadt.h / @c arch_madt.h
 * rather than this header.
 */
#ifndef _ACPI_ARCH_HOOKS_H_
#define _ACPI_ARCH_HOOKS_H_

#include "acpi_fadt.h"
#include "acpi_madt.h"
#include <rendezvos/error.h>

/** Filled before @c arch_parser_fadt runs. */
extern struct acpi_table_fadt *fadt_table;

/** Filled before @c arch_parser_madt runs. */
extern struct acpi_table_madt *madt_table;

/**
 * @brief Consume @c fadt_table .
 */
error_t arch_parser_fadt(void);

/**
 * @brief Consume @c madt_table .
 */
error_t arch_parser_madt(void);

#endif
