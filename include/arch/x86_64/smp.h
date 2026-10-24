#ifndef _RENDEZVOS_ARCH_SMP_H_
#define _RENDEZVOS_ARCH_SMP_H_
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/smp/percpu.h>
#define _RENDEZVOS_X86_64_AP_PHY_ADDR_ 0x1000
/*
    we put the ap entry code at 0x1000 (first page) when we try to mp set up
*/

/**
 * @brief x86 enabel AP cores.
 *
 * global start part:
 * - mark BSP enable
 * - Disables IRQ(no irq will interrupt this smp start).
 * - copy AP boot code to an trampoline with virtual page mapped (<1MiB, and
 * must be page-aligned, the ICR register is the page number)
 * - send INIT ipi to all APs
 *
 * percpu enable part:
 * - prepare the stack
 * - update the setup info for next AP
 * - send ipi and waiting for AP boot
 *
 * global end part:
 * - enable IRQ
 * - clean trampoline code
 * - finial clean the low addr map
 *
 * @param arch_setup_info Updated per AP before each SIPI
 */
void arch_start_smp(struct setup_info *arch_setup_info);

struct trap_frame;

/**
 * @brief Register @c ARCH_IRQ_ID_IPI (0x30) with @p handler and
 * @c IRQ_NEED_EOI.
 *
 * Called from @c smp_ipi_init on each CPU after init_interrupt / init_irq.
 * Destination @c cpu for the register path is this CPU's APIC id.
 */
void arch_smp_ipi_init(void (*handler)(struct trap_frame *tf));

/**
 * @brief Send IPI to @p cpu_id (APIC id) via ICR with vector @c
 * ARCH_IRQ_ID_IPI.
 *
 * @return REND_SUCCESS, or -E_RENDEZVOS if arch_irq_type is not xAPIC/x2APIC
 */
error_t arch_smp_ipi_send(cpu_id_t cpu_id);
#endif