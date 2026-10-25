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
#include <rendezvos/common.h>

/**
 * @brief Locate RSDP (today: IA-PC legacy BIOS path only).
 *
 * The legacy path ensures physical windows [0x80000,0x80400) and
 * [0xE0000,0x100000) via identity map, then scans at @c KERNEL_PHY_TO_VIRT.
 * No checksum. Other sources (e.g. UEFI) would be separate branches without
 * those ensures.
 *
 * @param[out] rsdp_out On success, identity VA of the RSDP.
 * @return @c REND_SUCCESS, or negative @c error_t (ensure failure / not found).
 */
error_t acpi_probe_rsdp(struct acpi_table_rsdp **rsdp_out);
/**
 * @brief Compare @c ACPI_SIG_LENG bytes of table signature.
 */
bool acpi_table_sig_check(char *acpi_table_sig_ptr, char *sig_chars);
/**
 * @brief Map table signature string to @c acpi_table_sig_enum.
 * @return Enum value, or -E_RENDEZVOS if unknown.
 */
enum acpi_table_sig_enum
get_acpi_table_type_from_sig(struct acpi_table_head *acpi_head);
/**
 * @brief Full ACPI bring-up after VSpace / Map_Handler are ready.
 *
 * Probes RSDP (legacy path maps its own windows), then ensures RSDT / child
 * tables and walks them (FACP / APIC → arch parsers).
 *
 * @param arch_setup_info Boot setup_info; unused on legacy BIOS today.
 *        UEFI may carry an RSDP hint (or related) here later.
 *
 * @return @c REND_SUCCESS, or negative @c error_t (no RSDP, bad revision,
 *         ensure/map failure, parser failure).
 */
error_t acpi_init(struct setup_info *arch_setup_info);
#endif
