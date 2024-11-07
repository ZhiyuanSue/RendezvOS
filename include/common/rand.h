/*
    This random function is learned from
   https://arvid.io/2018/07/02/better-cxx-prng/
*/
#include <common/types.h>

static inline u32 rand32(u64 seed)
{
        u64 ret = seed * 0xd989bcacc137dcd5ull;
        seed ^= seed >> 11;
        seed ^= seed << 31;
        seed ^= seed >> 18;
        return (u32)(ret >> 32ull);
}
static inline u64 rand64(u64 seed)
{
        u64 ret = rand32(seed);
        ret <<= 32;
        return ret + rand32(ret);
}