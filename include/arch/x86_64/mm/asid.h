#ifndef _RENDEZVOS_ARCH_ASID_H_
#define _RENDEZVOS_ARCH_ASID_H_

#include <common/types.h>

/**
 * @brief Hardware ASID/PCID identifier bit width.
 * @return 12 (PCID field width is 12 in CR3 when PCID is used).
 * @note PCID is still not enabled.
 */
static inline u32 arch_asid_supported_width(void)
{
        return 12;
}

#endif
