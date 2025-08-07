#ifndef _RENDEZVOS_TAGGEDPTR_H_
#define _RENDEZVOS_TAGGEDPTR_H_

#include <common/types.h>
#define TAG_BITS       16
#define ADDR_BITS      48
#define TAG_SHIFT      ADDR_BITS
#define TAG_MASK       ((1ULL << TAG_BITS) - 1)
#define ADDR_MASK      ((1ULL << ADDR_BITS) - 1)
#define ADDR_SIGN_MASK (1ULL << (ADDR_BITS - 1))

typedef u64 tagged_ptr_t;

static inline tagged_ptr_t tagged_ptr_pack(void* ptr, u16 tag)
{
        u64 tagged_ptr_value = 0;
        tagged_ptr_value |= (((u64)ptr) & ADDR_MASK);
        tagged_ptr_value |= (((u64)tag) << TAG_SHIFT);
        return tagged_ptr_value;
}
static inline void* tagged_ptr_unpack_ptr(tagged_ptr_t tp)
{
        u64 raw_address = tp & ADDR_MASK;
        if (raw_address & ADDR_SIGN_MASK) {
                return (void*)(raw_address | (~ADDR_MASK));
        } else {
                return (void*)raw_address;
        }
}
static inline u16 tagged_ptr_unpack_tag(tagged_ptr_t tp)
{
        return (u16)(tp >> TAG_SHIFT);
}
static inline tagged_ptr_t tagged_ptr_update_ptr(tagged_ptr_t tp, void* new_ptr)
{
        tp |= (((u64)new_ptr) & ADDR_MASK);
        return tp;
}
static inline tagged_ptr_t tagged_ptr_update_tag(tagged_ptr_t tp, u16 tag)
{
        tp |= (((u64)tag) << TAG_SHIFT);
        return tp;
}
#endif