#include <shampoos/mm/spmalloc.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
struct mem_allocator tmp_sp_alloctor;
static int slot_size[MAX_GROUP_SLOTS] =
        {8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048};

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
        if (chunk->nr_used_objs >= chunk->nr_max_objs) {
                /*
                        means no mem can use in this chunk
                        please check the return value
                */
                return NULL;
        }
        struct object_header* get_obj =
                (struct object_header*)(chunk->empty_obj_list.next);
        list_del(&get_obj->obj_list);
        list_add_head(&get_obj->obj_list, &chunk->partial_obj_list);
        chunk->nr_used_objs++;
        return get_obj;
}
error_t chunk_free_obj(struct object_header* obj, int allocator_id)
{
        /*allocator id must exist*/
        if (!obj || allocator_id < 0) {
                pr_error("[ERROR]illegal chunk free obj parameter\n");
                return -EPERM;
        }
        /*
        the legal obj must not be 4K aligned, but we do not check here
        if it's a 4K aligned, which means it is a page alloc, and should use the
        free pages
        but there's a problem that how can we know the pages we need to free?
        let's check the nexus realization.
        in nexus, for kernel space, one nexus entry record the page num.
        because it's ppn is continuous.
        but for user space, the ppn might not.
        so we have to let every entry record 4K or 2M.
        and here we must need the page_num to decide how much to return

        let's remember one thing: spmalloc is only used for kernel!!!!!!
        */
        struct mem_chunk* chunk =
                (struct mem_chunk*)ROUND_DOWN((vaddr)obj, PAGE_SIZE);
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]illegal chunk magic\n");
                return -EPERM;
        }
        /*this obj might be allocated by another allocator*/
        if (allocator_id != chunk->allocator_id)
                return chunk->allocator_id;
        list_del(&obj->obj_list);
        list_add_head(&obj->obj_list, &chunk->empty_obj_list);
        /*we do not clean it*/
        chunk->nr_used_objs--;
        return chunk->allocator_id;
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