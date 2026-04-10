#ifndef _RENDEZVOS_ARCH_ASID_H_
#define _RENDEZVOS_ARCH_ASID_H_

#include <common/stdbool.h>
#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>

static inline bool arch_asid_supports_16bit(void)
{
        u64 mmfr0 = 0;
        mrs("ID_AA64MMFR0_EL1", mmfr0);
        return ID_AA64MMFR0_EL1_GET_ASIDBITS(mmfr0)
               == ID_AA64MMFR0_EL1_ASIDBITS_16BIT;
}

#endif
