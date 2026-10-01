#ifndef _RENDEZVOS_AARCH64_POWER_CTRL_H_
#define _RENDEZVOS_AARCH64_POWER_CTRL_H_
#include <arch/aarch64/psci/psci.h>

/**
 * @brief Platform power-off entry: using psci_func.system_off().
 *
 * This function should be used after psci_init. 
 */
static inline void arch_shutdown(void)
{
        psci_func.system_off();
}

/**
 * @brief no reboot here, we don't realise it now
 */
static inline void arch_reset(void)
{
}

#endif