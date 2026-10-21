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
 * @brief Register @c ARCH_IRQ_ID_IPI (SGI0 → trap id) with @p handler and
 * @c IRQ_NEED_EOI.
 *
 * Called from @c smp_ipi_init after init_interrupt and
 * gic.init_cpu_interface. @p cpu for register is this CPU's dense id.
 */
void arch_smp_ipi_init(void (*handler)(struct trap_frame *tf));

/**
 * @brief Send SGI0 to @p cpu_id (dense id) via GIC.
 *
 * @return REND_SUCCESS, -E_IN_PARAM if cpu_id is illegal, or
 * -E_RENDEZVOS if GIC send_sgi is not set.
 */
error_t arch_smp_ipi_send(cpu_id_t cpu_id);
#endif