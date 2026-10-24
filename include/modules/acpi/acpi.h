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
 * found; on success returns a **VA** inside those already-mapped windows
 * (suitable for @c setup_info->rsdp_addr / @c acpi_init).
 *
 * @note The search windows must already be mapped. Mapping the RSDP is the
 * probe stage’s job—not @c acpi_init’s.
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
 * @brief Initialize ACPI from an already-mapped RSDP: ensure RSDT / child
 *        tables, walk RSDT, dispatch known signatures (e.g. FACP / APIC).
 *
 * @p rsdp_addr is a **virtual** address of the RSDP. The RSDP page must already
 * be accessible (legacy probe windows today; future UEFI paths must map before
 * storing this VA). This function does **not** map the RSDP itself—only RSDT
 * and secondary tables via @c ensure_kernel_identity_range.
 *
 * Typical producer: @c acpi_probe_rsdp from @c prepare_arch.
 */
error_t acpi_init(vaddr rsdp_addr);
#endif
