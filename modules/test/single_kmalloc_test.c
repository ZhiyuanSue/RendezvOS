// #define DEBUG
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/kmalloc.h>
#include <common/rand.h>
#include <common/string.h>
#include <rendezvos/smp/percpu.h>
extern struct allocator* kallocator;
extern int slot_size[MAX_GROUP_SLOTS];
struct bin {
        void* ptr;
        size_t size;
};
#define MAX_BIN        10086
#define MAX_ALLOC_SIZE 3520
#define PER_ITER_COUNT 96110
#define ITER_COUNT     2
u64 alloc_count = 0;
u64 free_count = 0;
static u64 next = 1;
static void k_chunk_print(struct mem_chunk* tmp_chunk)
{
        (void)tmp_chunk;
        debug("\t\t| chunk 0x%x obj total %d used %d\n",
              (vaddr)tmp_chunk,
              tmp_chunk->nr_max_objs,
              tmp_chunk->nr_used_objs);
}
static void k_allocator_group_print(struct mem_group* k_group)
{
        debug("\t\t| chunk free %d empty %d full %d\n",
              k_group->free_chunk_num,
              k_group->empty_chunk_num,
              k_group->full_chunk_num);
        struct list_entry* list_head = &k_group->empty_list;
        struct list_entry* tmp_list = list_head->next;
        while (tmp_list != list_head) {
                struct mem_chunk* tmp_chunk =
                        container_of(tmp_list, struct mem_chunk, chunk_list);
                k_chunk_print(tmp_chunk);
                tmp_list = tmp_list->next;
        }
}
static void k_allocator_print(struct mem_allocator* k_allocator_p)
{
        debug("\t| id %d\n", k_allocator_p->allocator_id);
        for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                debug("\t=== [ GROUP 0x%d] ===\n", slot_size[i]);
                k_allocator_group_print(&k_allocator_p->groups[i]);
        }
}
static void kmalloc_print(void)
{
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                if (per_cpu(kallocator, i)) {
                        debug("=== [ KMALLOC ] ===\n");
                        k_allocator_print(
                                (struct mem_allocator*)per_cpu(kallocator, i));
                }
        }
}
static int bin_check(struct bin* b)
{
        for (size_t i = 0; i < b->size; i++) {
                if (((char*)(b->ptr))[i]
                    != (char)((vaddr)(b->ptr) + b->size + i * 2)) {
                        return -E_REND_TEST;
                }
        }
        return 0;
}
static void bin_write(struct bin* b)
{
        for (size_t i = 0; i < b->size; i++) {
                ((char*)(b->ptr))[i] =
                        (char)((vaddr)(b->ptr) + b->size + i * 2);
        }
}
int kmalloc_test(void)
{
        /*first we try to alloc one 8 Bytes as basic test*/
        kmalloc_print();
        struct allocator* malloc = kallocator;
        void* test_alloc = malloc->m_alloc(malloc, 8);
        *((u64*)test_alloc) = 0;
        malloc->m_free(malloc, test_alloc);
        kmalloc_print();

        /*Then we alloc the bins space*/
        struct bin* b_array = (struct bin*)(malloc->m_alloc(
                malloc, sizeof(struct bin) * MAX_BIN));
        alloc_count++;
        if (!b_array) {
                pr_error("cannot alloc enough bins %d\n",
                         sizeof(struct bin) * MAX_BIN);
                return -E_REND_TEST;
        }
        memset(b_array, 0, sizeof(struct bin) * MAX_BIN);
        kmalloc_print();
        /*the main loop*/
        for (int iter = 0; iter < ITER_COUNT; iter++) {
                pr_info("kmalloc test iter %d\n", iter);
                /*alloc and free test*/
                for (int i = 0; i < PER_ITER_COUNT; i++) {
                        next = rand64(next);
                        struct bin* victim_b = &b_array[next % MAX_BIN];
                        if (victim_b->ptr) {
                                /*if have alloced ,just check and free it*/
                                int res = bin_check(victim_b);
                                if (res) {
                                        pr_error("check bin fail\n");
                                        kmalloc_print();
                                        return -E_REND_TEST;
                                }
                                malloc->m_free(malloc, victim_b->ptr);
                                free_count++;
                                victim_b->ptr = NULL;
                                victim_b->size = 0;
                        } else {
                                /*else try to alloc one*/
                                victim_b->size = rand64(next) % MAX_ALLOC_SIZE;
                                if (victim_b->size == 0) {
                                        continue;
                                }
                                victim_b->ptr =
                                        malloc->m_alloc(malloc, victim_b->size);
                                alloc_count++;
                                if (!(victim_b->ptr)) {
                                        pr_error("cannot get a obj\n");
                                        return -E_REND_TEST;
                                }
                                bin_write(victim_b);
                        }
                }
                kmalloc_print();
                /*clean all data for next iter*/
                for (int i = 0; i < MAX_BIN; i++) {
                        struct bin* b = &b_array[i];
                        if (b->ptr) {
                                malloc->m_free(malloc, b->ptr);
                                free_count++;
                                b->ptr = NULL;
                                b->size = 0;
                        }
                }
                kmalloc_print();
        }
        /*free b array*/
        malloc->m_free(malloc, b_array);
        free_count++;
        b_array = NULL;
        kmalloc_print();
        pr_info("total alloc %d times, and free %d times\n",
                alloc_count,
                free_count);
        if (alloc_count != free_count) {
                pr_error("alloc and free time unequal\n");
                return -E_REND_TEST;
        }
        return 0;
}