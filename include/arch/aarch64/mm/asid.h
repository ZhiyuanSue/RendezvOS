#ifndef _RENDEZVOS_ARCH_ASID_H_
#define _RENDEZVOS_ARCH_ASID_H_

#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>

/**
 * @brief Probe ID_AA64MMFR0_EL1 for hardware ASID bit width.
 * @return 16 if ASIDBITS reports 16-bit, else 8.
 * @note Does not program TCR. @c boot.S already sets @c TCR_EL1.AS when
 *       ASIDBits is 16-bit; this helper function only tell that capability for
 *       software.
 */
static inline u32 arch_asid_supported_width(void)
{
        u64 mmfr0 = 0;
        mrs("ID_AA64MMFR0_EL1", mmfr0);
        if (ID_AA64MMFR0_EL1_GET_ASIDBITS(mmfr0)
            == ID_AA64MMFR0_EL1_ASIDBITS_16BIT)
                return 16;
        return 8;
}

#endif
