#define DEBUG
#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/spmalloc.h>
extern struct allocator* allocator_pool[SHAMPOOS_MAX_CPU_NUMBER];
extern int slot_size[MAX_GROUP_SLOTS];
static void sp_chunk_print(struct mem_chunk* tmp_chunk)
{
        debug("\t\t| obj total %d used %d\n",
              tmp_chunk->nr_max_objs,
              tmp_chunk->nr_used_objs);
}
static void sp_allocator_group_print(struct mem_group* sp_group)
{
        debug("\t\t| chunk empty %d full %d\n",
              sp_group->empty_chunk_num,
              sp_group->full_chunk_num);
        struct list_entry* list_head = &sp_group->empty_list;
        struct list_entry* tmp_list = list_head->next;
        while (tmp_list != list_head) {
                struct mem_chunk* tmp_chunk =
                        container_of(tmp_list, struct mem_chunk, chunk_list);
                sp_chunk_print(tmp_chunk);
                tmp_list = tmp_list->next;
        }
}
static void sp_allocator_print(struct mem_allocator* sp_allocator_p)
{
        debug("\t| id %d\n", sp_allocator_p->allocator_id);
        for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                debug("\t=== [ GROUP 0x%d] ===\n", slot_size[i]);
                sp_allocator_group_print(&sp_allocator_p->groups[i]);
        }
}
static void spmalloc_print(void)
{
        for (int i = 0; i < SHAMPOOS_MAX_CPU_NUMBER; i++) {
                if (allocator_pool[i]) {
                        debug("=== [ SPMALLOC ] ===\n");
                        sp_allocator_print(
                                (struct mem_allocator*)allocator_pool[i]);
                }
        }
}
int spmalloc_test(void)
{
        spmalloc_print();
        struct allocator* malloc = allocator_pool[0];
        void* test_alloc = malloc->m_alloc(malloc, 8);
        *((u64*)test_alloc) = 0;
        malloc->m_free(malloc, test_alloc);
        return 0;
}