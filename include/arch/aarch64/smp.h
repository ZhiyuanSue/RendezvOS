#ifndef _RENDEZVOS_ARCH_SMP_H_
#define _RENDEZVOS_ARCH_SMP_H_
#include <arch/aarch64/boot/arch_setup.h>
#include <modules/dtb/dtb.h>
#include <arch/aarch64/psci/psci.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_id.h>

/**
 * @brief This function is used for aarch64 smp boot.
 * we only support enable-method=psci now(and the spin-table is not support)
 * the context_id(psci cpu_on third param) is used for passing cpu id.
 * we just boot other cpu one by one(wait for  each AP
 * CPU_STATE==enable before NR_CPU++), so we can reuse the setup_info
 * struct——only one cpu boot can use this structure at one time
 *
 * @param arch_setup_info the reused setup_info
 */
void arch_start_smp(struct setup_info *arch_setup_info);

struct trap_frame;

/**
 * @brief Register smp ipi handler @p handler (SGI0 → trap 64) with NEED_EOI
 * flag.
 *
 * Called from smp_ipi_init after init_interrupt and gic.init_cpu_interface.
 * 
 * It's the same with the x86_64 arch_smp_ipi_init.
 * But it's designed, for the riscv64 is using SSIP, which might not same.
 */
void arch_smp_ipi_init(void (*handler)(struct trap_frame *tf));

/**
 * @brief Using GIC interface to send ipi to target cpu
 * @param cpu the target cpu id.
 * @return REND_SUCCESS if success, -E_IN_PARAM if cpu id is illegal, or
 * -E_RENDEZVOS if GIC send_sgi interface is not set.
 */
error_t arch_smp_ipi_send(cpu_id_t cpu);
#endif