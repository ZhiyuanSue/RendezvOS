#ifndef _SHAMPOOS_ALLOCATOR_H_
#define _SHAMPOOS_ALLOCATOR_H_
#include <common/types.h>
#include <common/stddef.h>

#define MM_COMMON                                                       \
        struct allocator* (*init)(void* page_allocator);                \
        void* (*m_alloc)(struct allocator * allocator_p, size_t Bytes); \
        void (*m_free)(struct allocator * allocator_p, void* p);        \
        int allocator_id

struct allocator {
        MM_COMMON;
};

#endif