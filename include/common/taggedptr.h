#ifndef _RENDEZVOS_TAGGEDPTR_H_
#define _RENDEZVOS_TAGGEDPTR_H_

#include <common/types.h>
#include <common/stdbool.h>

/**
 * @file taggedptr.h
 * @brief Pack a canonical user/kernel pointer with a 16-bit tag in one u64.
 *
 * Layout: low @c ADDR_BITS (48) = address (sign-extended on read for
 * canonical form); high @c TAG_BITS (16) = tag. Callers may subdivide the
 * tag (e.g. ABA counter + append state via @c append_info_bits on an MSQ).
 * Pointers must be aligned so low address bits remain available for that
 * scheme.
 *
 * [中文临时对照 — 审阅后可删]
 * @file taggedptr.h
 * @brief 将规范形式的用户/内核指针与 16 位 tag 打包进一个 u64。
 * 布局：低 @c ADDR_BITS (48) = 地址（读出时做符号扩展以得规范形式）；
 * 高 @c TAG_BITS (16) = tag。调用方可细分 tag（例如经 MSQ 的
 * @c append_info_bits 放 ABA 计数 + append 状态）。指针须对齐，使地址域
 * 低位仍可供该方案使用。
 */

#define TAG_BITS       16
#define ADDR_BITS      48
#define TAG_SHIFT      ADDR_BITS
#define TAG_MASK       ((1ULL << TAG_BITS) - 1)
#define ADDR_MASK      ((1ULL << ADDR_BITS) - 1)
#define ADDR_SIGN_MASK (1ULL << (ADDR_BITS - 1))

typedef u64 tagged_ptr_t;

/**
 * @brief Empty / null tagged pointer .
 */
static inline tagged_ptr_t tp_new_none(void)
{
        return (u64)0;
}

/**
 * @brief True if @p tp is @c tp_new_none() .
 */
static inline bool tp_is_none(tagged_ptr_t tp)
{
        return tp == 0;
}

/**
 * @brief Pack @p ptr with @p tag (tag truncated to 16 bits, ptr truncated to 48
 * bits).
 */
static inline tagged_ptr_t tp_new(void* ptr, u16 tag)
{
        u64 tagged_ptr_value = 0;
        tagged_ptr_value |= (((u64)ptr) & ADDR_MASK);
        tagged_ptr_value |= (((u64)tag) << TAG_SHIFT);
        return tagged_ptr_value;
}

/**
 * @brief Extract pointer with canonical sign-extension of bit 47.
 */
static inline void* tp_get_ptr(tagged_ptr_t tp)
{
        u64 raw_address = tp & ADDR_MASK;
        if (raw_address & ADDR_SIGN_MASK) {
                return (void*)(raw_address | (~ADDR_MASK));
        } else {
                return (void*)raw_address;
        }
}

/**
 * @brief Extract the 16-bit tag.
 */
static inline u16 tp_get_tag(tagged_ptr_t tp)
{
        return (u16)(tp >> TAG_SHIFT);
}

/**
 * @brief Replace pointer bits; preserve tag.
 */
static inline void tp_set_ptr(tagged_ptr_t* tp, void* new_ptr)
{
        *tp = (*tp & ~ADDR_MASK) | (((u64)new_ptr) & ADDR_MASK);
}

/**
 * @brief Replace tag bits; preserve pointer.
 */
static inline void tp_set_tag(tagged_ptr_t* tp, u16 tag)
{
        *tp = (*tp & ~(TAG_MASK << TAG_SHIFT)) | (((u64)tag) << TAG_SHIFT);
}
#endif