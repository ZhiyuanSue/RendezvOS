/* As for pmm module,some are related to the platform, but some are not
 * So I easyly split it into two part that arch relative and no relative
 * */
#ifndef _SHAMPOOS_ARCH_PMM_H_
#define _SHAMPOOS_ARCH_PMM_H_

#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <modules/log/log.h>

void arch_init_pmm(struct setup_info* arch_setup_info);

#endif
