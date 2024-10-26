#include <shampoos/mm/spmalloc.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
struct mem_allocator tmp_sp_alloctor;

error_t chunk_init(struct mem_chunk* chunk, int chunk_order, int allocator_id)
{
        if (((vaddr)chunk) % PAGE_SIZE != 0 || chunk_order < 0
            || chunk_order >= MAX_GROUP_SLOTS) {
                pr_info("the chunk init parameter is wrong, please check\n");
                return -EPERM;
        }
        chunk->magic = CHUNK_MAGIC;
        chunk->chunk_order = chunk_order;
        chunk->allocator_id = allocator_id;
        INIT_LIST_HEAD(&chunk->chunk_list);
        INIT_LIST_HEAD(&chunk->partial_obj_list);
        INIT_LIST_HEAD(&chunk->empty_obj_list);
        chunk->nr_max_objs =
                sizeof(struct object_header) + slot_size[chunk_order];
        chunk->nr_used_objs = 0;
        int obj_num = (PAGE_SIZE * PAGE_PER_CHUNK - sizeof(struct mem_chunk))
                      / chunk->nr_max_objs;
        int padding_start =
                PAGE_SIZE * PAGE_PER_CHUNK - obj_num * chunk->nr_max_objs;
        for (int i = 0; i < obj_num; i++) {
                struct list_entry* obj_ptr =
                        (struct list_entry*)((vaddr)chunk + padding_start);
                list_add_head(obj_ptr, &chunk->empty_obj_list);
                padding_start += chunk->nr_max_objs;
        }
}
struct object_header* chunk_get_obj(struct mem_chunk* chunk)
{
        if (!chunk) {
                pr_error(
                        "[ERROR]the chunk get obj input parameter is wrong, please check\n");
                return NULL;
        }
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("bad chunk magic, please check\n");
                return NULL;
        }
        if(chunk->nr_used_objs== chunk->nr_max_objs){
                /*
                        means no mem can use in this chunk
                        please check the return value
                */
               return NULL;
        }
        
}
error_t chunk_free_obj(struct object_header* obj)
{
}

struct allocator* sp_init(void* nexus_root)
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