#ifndef _SHAMPOOS_PSCI_H_
#define _SHAMPOOS_PSCI_H_

#include <common/types.h>
#include <modules/dtb/dtb.h>
#include <modules/log/log.h>
enum psci_call_method { psci_call_none = 0, psci_call_msc, psci_call_hvc };
void psci_init(void);

#endif