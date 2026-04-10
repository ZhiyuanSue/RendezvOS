#ifndef _RENDEZVOS_BIT_H_
#define _RENDEZVOS_BIT_H_

#include "types.h"

/*please check the number of bits,we do no promise of the op*/

#define BIT_U8(n)  ((u8)(((u8)1u << (n))))
#define BIT_U16(n) ((u16)(((u16)1u << (n))))
#define BIT_U32(n) ((u32)(((u32)1u << (n))))
#define BIT_U64(n) ((u64)(1ull << (n)))

#define set_mask_u8(w, m)  ((u8)((u8)(w) | (u8)(m)))
#define set_mask_u16(w, m) ((u16)((u16)(w) | (u16)(m)))
#define set_mask_u32(w, m) ((u32)((u32)(w) | (u32)(m)))
#define set_mask_u64(w, m) ((u64)((u64)(w) | (u64)(m)))

#define clear_mask_u8(w, m)  ((u8)((u8)(w) & ~(u8)(m)))
#define clear_mask_u16(w, m) ((u16)((u16)(w) & ~(u16)(m)))
#define clear_mask_u32(w, m) ((u32)((u32)(w) & ~(u32)(m)))
#define clear_mask_u64(w, m) ((u64)((u64)(w) & ~(u64)(m)))

#define set_bit_u8(w, n)  set_mask_u8((w), BIT_U8(n))
#define set_bit_u16(w, n) set_mask_u16((w), BIT_U16(n))
#define set_bit_u32(w, n) set_mask_u32((w), BIT_U32(n))
#define set_bit_u64(w, n) set_mask_u64((w), BIT_U64(n))

#define clear_bit_u8(w, n)  clear_mask_u8((w), BIT_U8(n))
#define clear_bit_u16(w, n) clear_mask_u16((w), BIT_U16(n))
#define clear_bit_u32(w, n) clear_mask_u32((w), BIT_U32(n))
#define clear_bit_u64(w, n) clear_mask_u64((w), BIT_U64(n))

#define BCD_TO_BIN(bcd_code) \
        (((bcd_code) & 0xF) + 10 * (((bcd_code) >> 4) & 0xF))
#endif