#ifndef _RENDEZVOS_ALLOCATOR_H_
#define _RENDEZVOS_ALLOCATOR_H_
#include <common/types.h>
#include <common/stddef.h>

/**
 * @brief Allocator common fields shared by kernel heap implementations.
 * for different zone, different allocator might be used,
 * this common header contains common interface that the upper level ignore the
 * different realization
 */
#define MM_COMMON                                                       \
        struct allocator* (*init)(int allocator_id);                    \
        /*m_alloc must ensure the memory zero-filled*/                  \
        void* (*m_alloc)(struct allocator * allocator_p, size_t Bytes); \
        void (*m_free)(struct allocator * allocator_p, void* p);        \
        i64 allocator_id

struct allocator {
        MM_COMMON;
};

/**
 * @brief kallocator object instance ptr.
 * It's percpu, so use @c percpu(kallocator) / @c per_cpu(kallocator,id) to get
 * current cpu's kallocator.
 * This extern declaration is used for symbol global visible.
 */
extern struct allocator* kallocator;

#endif