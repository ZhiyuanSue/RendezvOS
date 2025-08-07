#ifndef _RENDEZVOS_ENDIAN_H_
#define _RENDEZVOS_ENDIAN_H_
#include <common/types.h>
/*In rendezvos we promise we use little endianness*/
static inline u16 SWAP_ENDIANNESS_16(u16 uint_16)
{
        return ((uint_16 & 0x00ff) << 8) + ((uint_16 & 0xff00) >> 8);
}
static inline u32 SWAP_ENDIANNESS_32(u32 uint_32)
{
        return ((uint_32 & 0x000000ff) << 24) + ((uint_32 & 0x0000ff00) << 8)
               + ((uint_32 & 0x00ff0000) >> 8) + ((uint_32 & 0xff000000) >> 24);
}
static inline u64 SWAP_ENDIANNESS_64(u64 uint_64)
{
        return ((uint_64 & 0x00000000000000ff) << 56)
               + ((uint_64 & 0x000000000000ff00) << 40)
               + ((uint_64 & 0x0000000000ff0000) << 24)
               + ((uint_64 & 0x00000000ff000000) << 8)
               + ((uint_64 & 0x000000ff00000000) >> 8)
               + ((uint_64 & 0x0000ff0000000000) >> 24)
               + ((uint_64 & 0x00ff00000000ff00) >> 40)
               + ((uint_64 & 0xff00000000000000) >> 56);
}

#endif