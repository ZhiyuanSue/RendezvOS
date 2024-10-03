#ifndef _SHAMPOOS_SLUB_H_
#define _SHAMPOOS_SLUB_H_

#include "pmm.h"
#include "vmm.h"
#include "nexus.h"
#include <common/types.h>
#include <common/dsa/list.h>
#define MAX_GROUP_SLOTS 12
static int slot_size[MAX_GROUP_SLOTS] =
        {8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048};
struct object_header {};
struct chunk {
        struct list_entry chunk_list;
        struct list_entry object_list;
};
struct sp_group {
        struct list_entry partial_list;
        struct list_entry empty_list;
};
struct sp_allocator {
        MM_COMMON;
        struct nexus_node* nexus_root;
        struct sp_group groups[MAX_GROUP_SLOTS];
};
struct allocator* sp_init(struct nexus_node* nexus_root);
void* sp_alloc(struct allocator* allocator_p, size_t Bytes);
void sp_free(struct allocator* allocator_p, void* p);

#endif