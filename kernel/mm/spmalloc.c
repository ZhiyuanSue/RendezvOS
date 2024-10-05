#include <shampoos/mm/spmalloc.h>
struct sp_allocator tmp_sp_alloctor;

static void init_chunk(vaddr chunk_addr, int chunk_order)
{
}
struct mem_obj* chunk_get_obj(struct mem_chunk* chunk)
{
}
error_t chunk_free_obj(struct mem_obj* obj)
{
}

struct allocator* sp_init(struct nexus_node* nexus_root)
{
        return (struct allocator*)&tmp_sp_alloctor;
}
void* sp_alloc(struct allocator* allocator_p, size_t Bytes)
{
        struct sp_allocator* sp_allocator_p = (struct sp_allocator*)allocator_p;

        return NULL;
}
void sp_free(struct allocator* allocator_p, void* p)
{
        struct sp_allocator* sp_allocator_p = (struct sp_allocator*)allocator_p;
}