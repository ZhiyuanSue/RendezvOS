#ifndef _RENDEZVOS_ARCH_SETUP_H_
#define _RENDEZVOS_ARCH_SETUP_H_
#include <common/types.h>
#include <rendezvos/smp/cpu_id.h>
#include "multiboot.h"
#include "multiboot2.h"

#ifndef KERNEL_VIRT_OFFSET
/** @brief High-half kernel virtual offset */
#define KERNEL_VIRT_OFFSET 0xffff800000000000
#endif
#ifndef USER_SPACE_TOP
#define USER_SPACE_TOP 0x00007ffffffff000
#endif
/** @brief Physical floor for usable RAM; below is BIOS.
 * This addr is fixed, because the real mode upper is here,
 * and if we go into protect mode, the memory under 2M always reserved
 */
#define BIOS_MEM_UPPER 0x100000

/**
 * @brief x86_64 boot structure shared by boot.S and cmain and
 * start_secondary_cpu. Offsets in field comments must match boot.S stores
 *
 * Multiboot info pointer is a physical address; convert with
 * GET_MULTIBOOT_INFO / GET_MULTIBOOT2_INFO after the high-half is mapped.
 */
struct setup_info {
        u32 multiboot_magic; /**0x0: multiboot magic: MB1 0x2BADB002 or MB2
                              * 0x36d76289. this magic is get from eax when
                              * start
                              */
        u32 multiboot_info_struct_ptr; /**0x4: Info struct physical address
                                        * (ebx). This address is get from ebx
                                        */
        u32 phy_addr_width; /**0x8: From CPUID 0x80000008 when bsp_entry.
                             * means the physicall address width
                             */
        u32 vir_addr_width; /*0xc: linear address width from same CPUID leaf*/
        vaddr rsdp_addr; /**0x10: Filled later by arch_init_pmm, 0 at entry.*/
        vaddr ap_boot_stack_ptr; /**0x18: SMP AP stack top
                                  * AP start one by one, every time using a new
                                  * ap_boot_stack so this ptr should update
                                  */
        cpu_id_t cpu_id; /*0x20: BSP/AP start given cpu id*/
} __attribute__((packed));

/**
 * @brief Get virtual Multiboot1 info pointer from @p setup_info.
 */
static inline struct multiboot_info *
GET_MULTIBOOT_INFO(struct setup_info *setup_info)
{
        return ((struct multiboot_info *)(setup_info->multiboot_info_struct_ptr
                                          + KERNEL_VIRT_OFFSET));
}

/**
 * @brief Get virtual Multiboot2 info pointer from @p setup_info.
 */
static inline struct multiboot2_info *
GET_MULTIBOOT2_INFO(struct setup_info *setup_info)
{
        return ((struct multiboot2_info *)(setup_info->multiboot_info_struct_ptr
                                           + KERNEL_VIRT_OFFSET));
}

/**
 * @brief Early arch prepare after cmain opens UART/log (before phy_mm_init).
 * Do some basic Multiboot(1 or 2) for later work.
 *
 * @param arch_setup_info Boot-filled setup_info (from boot.S).
 * @return 0 on success; -E_RENDEZVOS if magic is neither Multiboot1 nor 2.
 *
 * @note Portable name; aarch64 has the same symbol with DTB semantics.
 */
error_t prepare_arch(struct setup_info *arch_setup_info);

/**
 * @brief Fill global cpu_info via CPUID and set BSP_ID = APIC ID.
 *
 * @param arch_setup_info Unused just for the interface is same with other arch.
 * @return Always REND_SUCCESS.
 *
 * @note Called after arch_enable_percpu(BSP_ID) while BSP_ID may still be 0;
 *       this function may then rewrite BSP_ID by APIC ID.
 *       (So we can not always assume BSP ID is 0)
 */
error_t arch_cpu_info(struct setup_info *arch_setup_info);

/**
 * @brief platform bring-up after virt_mm_init let the kallocator available.
 *
 * - acpi_init
 * - scan pci
 *
 * @param arch_setup_info Must have rsdp_addr filled by arch_init_pmm.
 * @return REND_SUCCESS, or error from acpi_init / pci_scan_all.
 */
error_t arch_start_platform(struct setup_info *arch_setup_info);

/**
 * @brief Per-CPU arch bring-up (BSP and each AP).
 *
 * @param cpu_id Logical CPU id.
 * @return Always 0.
 */
error_t arch_start_core(cpu_id_t cpu_id);

#endif
