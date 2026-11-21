/*
 * We use SplitMix64 (Steele / Lea / Flood, OOPSLA 2014)，also see
 * https://arvid.io/2018/07/02/better-cxx-prng/
 *
 *   rng = splitmix64(&rng);           // tests: state in, scrambled out
 *   seed = splitmix64_seed_from(id);  // initial seed only (not a stream step)
 */
#ifndef _RENDEZVOS_COMMON_RAND_H_
#define _RENDEZVOS_COMMON_RAND_H_

#include <common/types.h>

#define SPLITMIX64_GAMMA     0x9e3779b97f4a7c15ULL /* golden-ratio Weyl step */
#define SPLITMIX64_MIX_A     0xbf58476d1ce4e5b9ULL /* published avalanche */
#define SPLITMIX64_MIX_B     0x94d049bb133111ebULL
#define SPLITMIX64_SEED_BASE 0x6a09e667f3bcc909ULL /* frac(sqrt(2)); non-zero \
                                                    */

/** Initial seed from @p salt (e.g. cpu id). */
static inline u64 splitmix64_seed_from(u64 salt)
{
        return SPLITMIX64_SEED_BASE ^ (salt * SPLITMIX64_GAMMA);
}

/** One step: advance @p *state, return mixed u64. */
static inline u64 splitmix64(u64 *state)
{
        u64 z;

        z = (*state += SPLITMIX64_GAMMA);
        z = (z ^ (z >> 30)) * SPLITMIX64_MIX_A;
        z = (z ^ (z >> 27)) * SPLITMIX64_MIX_B;
        return z ^ (z >> 31);
}

#endif /* _RENDEZVOS_COMMON_RAND_H_ */
