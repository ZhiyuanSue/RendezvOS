#ifndef _RENDEZVOS_ARCH_AARCH64_RANDOM_H_
#define _RENDEZVOS_ARCH_AARCH64_RANDOM_H_

#include <common/stdbool.h>
#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>

/**
 * @brief Read one hardware random u64 via RNDR when FEAT_RNG is present.
 *
 * Probes @c ID_AA64ISAR0_EL1.RNDR first so unsupported CPUs never execute
 * the MRS. On success RNDR clears NZCV; on failure it sets Z — we use
 * @c cset …,ne. Does not fall back to a software PRNG.
 *
 * @param val Non-NULL; written only on success.
 * @return true if @p val was filled from RNDR.
 */
static inline bool arch_get_random_long(u64 *val)
{
        u64 isar0;
        u64 word;
        u32 ok;

        if (!val)
                return false;

        mrs("ID_AA64ISAR0_EL1", isar0);
        if (ID_AA64ISAR0_EL1_GET_RNDR(isar0) == ID_AA64ISAR0_EL1_RNDR_NONE)
                return false;

        for (int i = 0; i < 10; i++) {
                __asm__ __volatile__("mrs %0, " AARCH64_SYSREG_RNDR "\n\t"
                                     "cset %w1, ne"
                                     : "=r"(word), "=r"(ok)
                                     :
                                     : "cc");
                if (ok) {
                        *val = word;
                        return true;
                }
        }
        return false;
}

#endif /* _RENDEZVOS_ARCH_AARCH64_RANDOM_H_ */
