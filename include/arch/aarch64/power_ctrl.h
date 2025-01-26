#ifndef _SHAMPOOS_AARCH64_POWER_CTRL_H_
#define _SHAMPOOS_AARCH64_POWER_CTRL_H_
#include <modules/psci/psci.h>

static inline void arch_shutdown(void)
{
        psci_system_off();
}

static inline void arch_reset(void)
{
}

#endif