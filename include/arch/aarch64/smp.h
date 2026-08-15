#ifndef _RENDEZVOS_ARCH_SMP_H_
#define _RENDEZVOS_ARCH_SMP_H_
#include <arch/aarch64/boot/arch_setup.h>
#include <modules/dtb/dtb.h>
#include <arch/aarch64/psci/psci.h>
void arch_start_smp(struct setup_info *arch_setup_info);
#endif