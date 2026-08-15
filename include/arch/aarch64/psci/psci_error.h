#ifndef _RENDEZVOS_PSCI_ERROR_H_
#define _RENDEZVOS_PSCI_ERROR_H_

enum psci_error_code {
        psci_succ = 0,
        psci_not_support,
        psci_invalid_parameter,
        psci_denied,
        psci_already_on,
        psci_on_pending,
        psci_internel_failure,
        psci_not_present,
        psci_disabled,
        psci_invalid_address,
};

#endif