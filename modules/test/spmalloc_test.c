#define DEBUG
#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/spmalloc.h>
#include <common/rand.h>
extern struct allocator* allocator_pool[SHAMPOOS_MAX_CPU_NUMBER];
extern int slot_size[MAX_GROUP_SLOTS];
struct bin {
        void* ptr;
        size_t size;
};
#define MAX_BIN        10086
#define MAX_ALLOC_SIZE 6166
#define PER_ITER_COUNT 96110
#define ITER_COUNT     10
static u64 next = 1;
static void sp_chunk_print(struct mem_chunk* tmp_chunk)
{
        debug("\t\t| chunk 0x%x obj total %d used %d\n",
              (vaddr)tmp_chunk,
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
int bin_check(struct bin* b)
{
        return 0;
}
int spmalloc_test(void)
{
        /*first we try to alloc one 8 Bytes as basic test*/
        spmalloc_print();
        struct allocator* malloc = allocator_pool[0];
        void* test_alloc = malloc->m_alloc(malloc, 8);
        spmalloc_print();
        *((u64*)test_alloc) = 0;
        malloc->m_free(malloc, test_alloc);

        /*Then we alloc the bins space*/
        struct bin* b_array = (struct bin*)(malloc->m_alloc(
                malloc, sizeof(struct bin) * MAX_BIN));
        if (!b_array) {
                pr_error("cannot alloc enough bins %d\n",
                         sizeof(struct bin) * MAX_BIN);
                return -1;
        }
        /*the main loop*/
        for (int iter = 0; iter < ITER_COUNT; iter++) {
                debug("spmalloc test iter %d\n", iter);
                /*alloc and free test*/
                for (int i = 0; i < PER_ITER_COUNT; i++) {
                        next = rand64(next);
                        struct bin* victim_b = &b_array[next % MAX_BIN];
                        if (victim_b->ptr) {
                                /*if have alloced ,just check and free it*/
                                int res = bin_check(victim_b);
                                if (res) {
                                        pr_error("check bin fail\n");
                                        return -1;
                                }
                                malloc->m_free(malloc, victim_b->ptr);
                                victim_b->ptr = NULL;
                                victim_b->size = 0;
                        } else {
                                /*else try to alloc one*/
                                victim_b->size = rand64(next) % MAX_ALLOC_SIZE;
                                victim_b->ptr =
                                        malloc->m_alloc(malloc, victim_b->size);
                        }
                }
                /*clean all data for next iter*/
                for (int i = 0; i < MAX_BIN; i++) {
                        struct bin* b = &b_array[i];
                        if (b->ptr) {
                                malloc->m_free(malloc, b->ptr);
                                b->ptr = NULL;
                                b->size = 0;
                        }
                }
        }
        /*free b array*/
        malloc->m_free(malloc, b_array);
        b_array = NULL;
        spmalloc_print();
        return 0;
}