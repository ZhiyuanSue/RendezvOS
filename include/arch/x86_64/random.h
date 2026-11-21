#ifndef _RENDEZVOS_ARCH_X86_64_RANDOM_H_
#define _RENDEZVOS_ARCH_X86_64_RANDOM_H_

#include <common/stdbool.h>
#include <common/types.h>
#include <arch/x86_64/cpuinfo.h>

/**
 * @brief Read one hardware random u64 via RDRAND when CPUID.1:ECX.RDRAND=1.
 *
 * Retries RDRAND_RETRY_TIME times if CF=0.
 *
 * @param val Non-NULL; written only on success.
 * @return true if @p val was filled from RDRAND.
 */
#define RDRAND_RETRY_TIME 10
static inline bool arch_get_random_long(u64 *val)
{
        u64 word;
        u8 ok;

        if (!val||!(cpu_info.feature_1 & X86_CPUID_FEATURE_ECX_RDRAND))
                return false;

        for (int i = 0; i < RDRAND_RETRY_TIME; i++) {
                __asm__ __volatile__("rdrand %0\n\t"
                                     "setc %1"
                                     : "=r"(word), "=qm"(ok)
                                     :
                                     : "cc");
                if (ok) {
                        *val = word;
                        return true;
                }
        }
        return false;
}

#endif /* _RENDEZVOS_ARCH_X86_64_RANDOM_H_ */
