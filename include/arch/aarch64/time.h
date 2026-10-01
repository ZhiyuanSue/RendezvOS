#ifndef _RENDEZVOS_ARCH_TIME_
#define _RENDEZVOS_ARCH_TIME_
#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>
#include <common/stdbool.h>
/*CNTFRQ_EL0*/

/*CNTV_CTL_EL0*/
#define CNTV_CTL_EL0_ENABLE  (0x1)
#define CNTV_CTL_EL0_IMASK   (0x1 << 1)
#define CNTV_CTL_EL0_ISTATUS (0x1 << 2)

/*
 * arm,armv8-timer interrupts[] entry order
 * (each entry = 3 GIC cells: type, irq, flags — see gic_dt.h).
 * Binding order:
 *   secure EL1 physical
 *   non-secure EL1 physical (CNTP),
 *   virtual (CNTV)
 *   hypervisor physical (CNTHP).
 */
#define ARCH_TIMER_SEC_PHYS 0u
#define ARCH_TIMER_NS_PHYS  1u
#define ARCH_TIMER_VIRT     2u
#define ARCH_TIMER_HYP_PHYS 3u

#endif