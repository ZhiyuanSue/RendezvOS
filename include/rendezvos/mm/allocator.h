#ifndef _RENDEZVOS_ALLOCATOR_H_
#define _RENDEZVOS_ALLOCATOR_H_
#include <common/types.h>
#include <common/stddef.h>

#define MM_COMMON                                                       \
        struct allocator* (*init)(struct nexus_node * page_allocator,   \
                                  int allocator_id);                    \
        void* (*m_alloc)(struct allocator * allocator_p, size_t Bytes); \
        void (*m_free)(struct allocator * allocator_p, void* p);        \
        i64 allocator_id

struct allocator {
        MM_COMMON;
};

#endif