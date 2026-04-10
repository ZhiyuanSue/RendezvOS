#ifndef _RENDEZVOS_ARCH_ASID_H_
#define _RENDEZVOS_ARCH_ASID_H_

#include <common/stdbool.h>

static inline bool arch_asid_supports_16bit(void)
{
        return true;
}

#endif
