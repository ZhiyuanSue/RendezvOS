// #define DEBUG
#include <modules/test/test.h>
#include <rendezvos/mm/nexus.h>
#include <modules/log/log.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/spmalloc.h>
#include <common/rand.h>
#include <common/string.h>
#include <rendezvos/percpu.h>
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
DEFINE_PER_CPU(u64, smp_alloc_count);
DEFINE_PER_CPU(u64, smp_free_count);
DEFINE_PER_CPU(u64, smp_next);
static void sp_chunk_print(struct mem_chunk* tmp_chunk)
{
        debug("\t\t| chunk 0x%x obj total %d used %d\n",
              (vaddr)tmp_chunk,
              tmp_chunk->nr_max_objs,
              tmp_chunk->nr_used_objs);
}
static void sp_allocator_group_print(struct mem_group* sp_group)
{
        debug("\t\t| chunk free %d empty %d full %d\n",
              sp_group->free_chunk_num,
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
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                if (per_cpu(kallocator, i)) {
                        debug("=== [ SPMALLOC ] ===\n");
                        sp_allocator_print(
                                (struct mem_allocator*)per_cpu(kallocator, i));
                }
        }
}
static int bin_check(struct bin* b)
{
        for (int i = 0; i < b->size; i++) {
                if (((char*)(b->ptr))[i]
                    != (char)((vaddr)(b->ptr) + b->size + i * 2)) {
                        return -1;
                }
        }
        return 0;
}
static void bin_write(struct bin* b)
{
        for (int i = 0; i < b->size; i++) {
                ((char*)(b->ptr))[i] =
                        (char)((vaddr)(b->ptr) + b->size + i * 2);
        }
}
int smp_spmalloc_test(void)
{
        /*first we try to alloc one 8 Bytes as basic test*/
        // spmalloc_print();
        struct allocator* malloc = percpu(kallocator);
        pr_info("[CPU:%d]\t[allocator\t@\t%x]\n\t[nexus_root\t@\t%x]\n\t[handler\t@\t%x]\n",
                percpu(cpu_number),
                malloc,
                ((struct mem_allocator*)malloc)->nexus_root,
                ((struct mem_allocator*)malloc)->nexus_root->handler);
        void* test_alloc = malloc->m_alloc(malloc, 8);
        *((u64*)test_alloc) = 0;
        malloc->m_free(malloc, test_alloc);
        spmalloc_print();

        /*Then we alloc the bins space*/
        struct bin* b_array = (struct bin*)(malloc->m_alloc(
                malloc, sizeof(struct bin) * MAX_BIN));
        percpu(smp_alloc_count)++;
        if (!b_array) {
                pr_error("cannot alloc enough bins %d\n",
                         sizeof(struct bin) * MAX_BIN);
                return -1;
        }
        memset(b_array, 0, sizeof(struct bin) * MAX_BIN);
        spmalloc_print();
        /*the main loop*/
        for (int iter = 0; iter < ITER_COUNT; iter++) {
                pr_info("spmalloc test iter %d\n", iter);
                /*alloc and free test*/
                for (int i = 0; i < PER_ITER_COUNT; i++) {
                        percpu(smp_next) = rand64(percpu(smp_next));
                        struct bin* victim_b =
                                &b_array[percpu(smp_next) % MAX_BIN];
                        if (victim_b->ptr) {
                                /*if have alloced ,just check and free it*/
                                int res = bin_check(victim_b);
                                if (res) {
                                        pr_error("check bin fail\n");
                                        spmalloc_print();
                                        return -1;
                                }
                                malloc->m_free(malloc, victim_b->ptr);
                                percpu(smp_free_count)++;
                                victim_b->ptr = NULL;
                                victim_b->size = 0;
                        } else {
                                /*else try to alloc one*/
                                victim_b->size = rand64(percpu(smp_next))
                                                 % MAX_ALLOC_SIZE;
                                if (victim_b->size == 0) {
                                        continue;
                                }
                                victim_b->ptr =
                                        malloc->m_alloc(malloc, victim_b->size);
                                percpu(smp_alloc_count)++;
                                if (!(victim_b->ptr)) {
                                        pr_error("cannot get a obj\n");
                                        return -1;
                                }
                                bin_write(victim_b);
                        }
                }
                spmalloc_print();
                /*clean all data for next iter*/
                for (int i = 0; i < MAX_BIN; i++) {
                        struct bin* b = &b_array[i];
                        if (b->ptr) {
                                malloc->m_free(malloc, b->ptr);
                                percpu(smp_free_count)++;
                                b->ptr = NULL;
                                b->size = 0;
                        }
                }
                spmalloc_print();
        }
        /*free b array*/
        malloc->m_free(malloc, b_array);
        percpu(smp_free_count)++;
        b_array = NULL;
        spmalloc_print();
        pr_info("total alloc %d times, and free %d times\n",
                percpu(smp_alloc_count),
                percpu(smp_free_count));
        if (percpu(smp_alloc_count) != percpu(smp_free_count)) {
                pr_error("alloc and free time unequal\n");
                return -1;
        }
        return 0;
}