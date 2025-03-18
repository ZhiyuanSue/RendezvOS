#ifndef _RENDEZVOS_SLUB_H_
#define _RENDEZVOS_SLUB_H_

#include "pmm.h"
#include "vmm.h"
#include "nexus.h"
#include <common/types.h>
#include <common/dsa/list.h>
#include <rendezvos/mm/allocator.h>
#define MAX_GROUP_SLOTS 12
#define PAGE_PER_CHUNK  2
#define CHUNK_MAGIC     0xa11ca11ca11ca11c
/*
        In system there might have multiple spmalloc structs
        e.x. as a per cpu allocator
        so there's no lock here
        but, it might have lock in get_free_page and free_page

        and as for the spmalloc
        in one spmalloc, it have NR_CHUNK linked list groups
        and in those linked list groups, the objects size is defined in obj_size
   array below

        every linked list group have 2 linked list
                one is full or partial, one is empty
                for empty chunk might be reused

                if one group lack of memory
                it will first try to find other groups for pages
                if no page, then try to use get free page

                when a partial page is all empty
                it also first put into the empty list
                only the empty list have tooo many page will it return to the
   system

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

   Remember: there might have multiple allocator in system, and we do not do
   this part
   - alloc is always success, but free might not
   - for a free op, it will try to find the chunk in this allocator, but might
   in another allocator we have to use lock to realize it
*/

struct object_header {
        struct list_entry obj_list;
        char obj[];
};
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
};
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
};
struct mem_allocator {
        MM_COMMON;
        struct nexus_node* nexus_root;
        struct mem_group groups[MAX_GROUP_SLOTS];
};
/*chunk*/
error_t chunk_init(struct mem_chunk* chunk, int chunk_order, int allocator_id);
struct object_header* chunk_get_obj(struct mem_chunk* chunk);
error_t chunk_free_obj(struct object_header* obj, int allocator_id);
struct allocator* sp_init(struct nexus_node* nexus_root, int allocator_id);
void* sp_alloc(struct allocator* allocator_p, size_t Bytes);
void sp_free(struct allocator* allocator_p, void* p);

#endif