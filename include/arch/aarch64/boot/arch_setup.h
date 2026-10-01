#ifndef _RENDEZVOS_ARCH_SETUP_H_
#define _RENDEZVOS_ARCH_SETUP_H_
#include <common/types.h>
#include <rendezvos/smp/cpu_id.h>
#ifndef KERNEL_VIRT_OFFSET
/** @brief High-half kernel virtual offset */
#define KERNEL_VIRT_OFFSET 0xffff800000000000
#endif
#ifndef USER_SPACE_TOP
#define USER_SPACE_TOP 0x00007ffffffff000
#endif

/**
 * @brief Linux arm64 Image header, which is packed at image start,
 * see boot.S, and is loaded by firmware/QEMU.
 */
struct boot_header {
        u32 code0;
        u32 code1;
        u64 text_offset;
        u64 image_size;
        u64 flags;
        u64 res2;
        u64 res3;
        u64 res4;
        u32 magic;
        u32 res5;
} __attribute__((packed));

/**
 * @brief aarch64 boot structure shared by boot.S and cmain and start_secondary_cpu.
 * Offsets in field comments must match boot.S stores
 */
struct setup_info {
        u64 dtb_ptr; /*0x0: DTB physical addr passed by fireware
                      * but will relocated by boot_map as boot_dtb_header_base_addr
                      */
        u64 res_x1; /*0x8*/
        u64 res_x2; /*0x10*/
        u64 res_x3; /*0x18*/
        u64 map_end_virt_addr; /*0x20: end virtural address of kernel boot map work
                                * the pmm and vmm should start after here
                                */
        u64 boot_uart_base_addr; /*0x28: PL011 MMIO base 
                                  * just for early uart output
                                  */
        u64 boot_dtb_header_base_addr; /*0x30: DTB header kernel VA
                                        * the boot map stage should mov dtb ptr to property position
                                        */
        vaddr ap_boot_stack_ptr; /*0x38: SMP AP stack top
                                  * AP start one by one, every time using a new ap_boot_stack
                                  * so this ptr should update
                                  */
        cpu_id_t cpu_id; /*0x40: BSP/AP start given cpu id*/
};

/**
 * @brief Map the copied and aligned DTB into the high half and validate FDT header.
 *
 * @param arch_setup_info Boot-filled setup_info (dtb_ptr already relocated).
 * @return 0 on success; -E_RENDEZVOS if fdt_check_header fails.
 */
error_t prepare_arch(struct setup_info *arch_setup_info);

/**
 * @brief Parse MPIDR_EL1 and try to get the cpu infomation.
 *
 * @param arch_setup_info Unused just for the interface is same with other arch.
 * @return Always REND_SUCCESS.
 */
error_t arch_cpu_info(struct setup_info *arch_setup_info);

/**
 * @brief platform bring-up after virt_mm_init let the kallocator available.
 * - Parser DTB and build device tree
 * - handle cmdline based on device tree
 * - system psci init
 * - gic interrupt controller distributor init
 *
 * @param arch_setup_info Must have boot_dtb_header_base_addr from prepare_arch.
 * @return Always REND_SUCCESS in the current implementation
 *
 * @note GIC CPU interface is not initialized here — it's percpu.
 */
error_t arch_start_platform(struct setup_info *arch_setup_info);

/**
 * @brief Per-CPU arch bring-up (BSP and each AP).
 *
 * Order in implementation:
 * - init_interrupt
 * - gic.init_cpu_interface
 * - smp_ipi_init
 * - rendezvos_time_init
 * - init_syscall.
 *
 * @param cpu_id Logical CPU id for this core.
 * @return Always 0
 *
 * @note Distributor must already be up from arch_start_platform on BSP.
 */
error_t arch_start_core(cpu_id_t cpu_id);

#endif
