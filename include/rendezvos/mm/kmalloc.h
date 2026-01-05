#ifndef _RENDEZVOS_SLUB_H_
#define _RENDEZVOS_SLUB_H_

#include "pmm.h"
#include "vmm.h"
#include "nexus.h"
#include <common/types.h>
#include <common/dsa/list.h>
#include <common/dsa/rb_tree.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/sync/cas_lock.h>
#define MAX_GROUP_SLOTS 12
#define PAGE_PER_CHUNK  4
#define CHUNK_MAGIC     0xa11ca11ca11ca11c
/*
        In one kmalloc, it have NR_CHUNK linked list groups
        and in those linked list groups, the objects size is defined in obj_size
   array below

        every linked list group have 2 linked list
        one is full or partial, one is empty,
        empty chunk might be reused

        if one group lack of memory,
        it will first try to find other groups for pages,
        if no page find, then try to use get_free_page

        when a partial page is all empty
        it also first put into the empty list
        if the empty list have tooo many page will it return to the system

        every linked list have some linked chunks
        like:
        | chunk | -> | chunk | -> ...
        (actually, use double linked list)

        in every chunk, it might have several pages
        the page size of every chunk is difined as nr_chunk_page

        (This is because, for a chunk is 1 page with 4096 size ,
        and if the object in this chunk is 2048,
        and I also have a header in every chunk,
        so I will waste 2048 - header size space, it waste toooo much,
        but if I use a chunk with 8 pages, only about 1/16 is wasted )

        and in on chunk, the space is like :
        -------------------------------------
        | header | header padding | objects |
        -------------------------------------
        the header padding of every linked list with different object size is
   different and is defined in chunk_header_padding_size array

        then is the objects
        +----------------------------+----------------------------+
        || obj_header | obj_payload ||| obj_header | obj_payload ||...
        +----------------------------+----------------------------+
        the obj_header is just a list entry, for used list and empty list in
   this chunk

   Remember: there might have multiple allocator in system, for a free op, it
   will try to find the chunk in this allocator, but might in another allocator
   we have to use lock to realize it
*/

struct page_chunk_node {
        struct rb_node _rb_node;
        vaddr page_addr;
        i64 page_num;
} __attribute__((aligned(sizeof(u64))));
struct object_header {
        struct list_entry obj_list;
        i64 allocator_id;
        ms_queue_node_t msq_node;
        char obj[];
} __attribute__((aligned(sizeof(u64))));
struct mem_chunk {
        u64 magic; /*the magic of mem_chunk is 0xa11ca11ca11ca11c*/
        int allocator_id;
        int chunk_order;
        int nr_max_objs;
        int nr_used_objs;
        struct list_entry chunk_list;
        struct list_entry full_obj_list;
        struct list_entry empty_obj_list;
        char padding[];
} __attribute__((aligned(sizeof(u64))));
struct mem_group {
        int allocator_id;
        int chunk_order;
        size_t free_chunk_num; /*free chunk means which can freely moved between
                                  groups*/
        size_t empty_chunk_num; /*empty means we have empty objects chunk*/
        size_t full_chunk_num; /*full means all the objects have been
                                  allocated*/
        struct list_entry full_list;
        struct list_entry empty_list;
        /*the free and empty chunks are all linked in empty_list*/
} __attribute__((aligned(sizeof(u64))));
struct mem_allocator {
        MM_COMMON;
        struct nexus_node* nexus_root;
        struct mem_group groups[MAX_GROUP_SLOTS];
        struct rb_root page_chunk_root;
        ms_queue_t buffer_msq;
        atomic64_t buffer_size;
        cas_lock_t lock;
} __attribute__((aligned(sizeof(u64))));
/*chunk*/
struct allocator* kinit(struct nexus_node* nexus_root, int allocator_id);

#endif