/*
 * It's a realization of ACPI Specification for version 6.5
 * And I rewrite it .
 * The ARM is trying to using UEFI + ACPI for devices. so it must be a common
 * module and x86_64 and aarch64 must have arch_* parser work.
 */
#ifndef _ACPI_H_
#define _ACPI_H_

#include "acpi_table.h"
#include "acpi_fadt.h"
#include "acpi_madt.h"
#include <common/stdbool.h>

/**
 * @brief Locate RSDP (today: IA-PC legacy BIOS windows via internal helper).
 *
 * @p search_start_vaddr is the virt base for low physical identity used by
 * the legacy BIOS scan, which searches [ @p search_start_vaddr + 0x80000, @p
 * search_start_vaddr + 0x80400) then [ @p search_start_vaddr +0xE0000, @p
 * search_start_vaddr +0x100000). No checksum validation. Returns NULL if not
 * found.
 *
 * @note The search window must already be mapped to a virtual range.
 */
struct acpi_table_rsdp* acpi_probe_rsdp(vaddr search_start_vaddr);
/**
 * @brief Compare @c ACPI_SIG_LENG bytes of table signature.
 */
bool acpi_table_sig_check(char* acpi_table_sig_ptr, char* sig_chars);
/**
 * @brief Map table signature string to @c acpi_table_sig_enum.
 * @return Enum value, or -E_RENDEZVOS if unknown.
 */
enum acpi_table_sig_enum
get_acpi_table_type_from_sig(struct acpi_table_head* acpi_head);
/**
 * @brief Initialize ACPI from RSDP: check RSDT revision and parse known
 *        tables (e.g. FACP / APIC).
 *
 * @p rsdp_addr must be a valid RSDP virtual address.(must using acpi_probe_rsdp
 * to search it)
 */
error_t acpi_init(vaddr rsdp_addr);
#endif
