#include <modules/test/test.h>
#include <modules/log/log.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/spmalloc.h>
extern struct allocator* kallocator;
struct ms_test_data {
        i64 allocator_id;
        ms_queue_node_t ms_node;
        u64 data;
};
extern u32 BSP_ID;
#define percpu_ms_queue_test_number 10000
#ifdef NR_CPUS
#define ms_data_len percpu_ms_queue_test_number* NR_CPUS / 2
#else
#define ms_data_len \
        percpu_ms_queue_test_number * 4 / 2 /*the default of NR_CPU is 4*/
#endif
struct ms_test_data ms_data[ms_data_len];
struct ms_test_data dummy;
ms_queue_t ms_queue;
static volatile bool have_inited = false;
static volatile bool dyn_have_inited = false;
static cas_lock_t cas_lock;
static volatile int cas_add_value = 0;
int ms_data_test_seq[ms_data_len] = {0};
void smp_ms_queue_init(void)
{
        dummy.data = -1;
        msq_init(&ms_queue, &dummy.ms_node);
        for (int i = 0; i < ms_data_len; i++) {
                ms_data[i].ms_node.next = tp_new_none();
                ms_data[i].data = i;
        }
}
void smp_ms_queue_put(int offset)
{
        for (int i = 0; i < percpu_ms_queue_test_number; i++) {
                msq_enqueue(&ms_queue, &ms_data[offset + i].ms_node);
        }
}
void smp_ms_queue_get(int offset)
{
        int i = 0;
        while (i < percpu_ms_queue_test_number) {
                tagged_ptr_t dequeue_ptr = tp_new_none();
                while ((dequeue_ptr = msq_dequeue(&ms_queue)) == 0)
                        ;
                struct ms_test_data* get_ptr = container_of(
                        tp_get_ptr(dequeue_ptr), struct ms_test_data, ms_node);
                ms_data_test_seq[offset + i] = get_ptr->data;
                i++;
        }
}
int smp_ms_queue_test(void)
{
        if (percpu(cpu_number) == BSP_ID) {
                smp_ms_queue_init();
                have_inited = true;
        } else {
                while (!have_inited)
                        ;
        }
        if (percpu(cpu_number) % 2) {
                smp_ms_queue_put((percpu(cpu_number) / 2)
                                 * percpu_ms_queue_test_number);
        } else {
                smp_ms_queue_get((percpu(cpu_number) / 2)
                                 * percpu_ms_queue_test_number);
        }
        return 0;
}
bool smp_ms_queue_check(void)
{
        // if (percpu(cpu_number) == BSP_ID) {
        //         for (int i = 0; i < ms_data_len; i++) {
        //                 if (i % 10 == 0 && i)
        //                         pr_info("\n");
        //                 pr_info("%d\t", ms_data_test_seq[i]);
        //         }
        //         pr_info("\n");
        // }
        return true;
}

void smp_ms_queue_dyn_alloc_init(void)
{
        struct allocator* malloc = percpu(kallocator);
        struct ms_test_data* tmp =
                malloc->m_alloc(malloc, sizeof(struct ms_test_data));
        tmp->data = -1;
        tmp->ms_node.next = tp_new_none();
        tmp->allocator_id = percpu(cpu_number);
        msq_init(&ms_queue, &tmp->ms_node);
        memset(ms_data_test_seq, 0, ms_data_len * sizeof(int));
}
void smp_ms_queue_dyn_alloc_put(int offset)
{
        struct allocator* malloc = percpu(kallocator);
        int allocator_id = malloc->allocator_id;
        for (int i = offset; i < offset + percpu_ms_queue_test_number; i++) {
                struct ms_test_data* tmp_ms_data =
                        malloc->m_alloc(malloc, sizeof(struct ms_test_data));
                tmp_ms_data->data = i;
                tmp_ms_data->allocator_id = allocator_id;
                tmp_ms_data->ms_node.next = tp_new_none();
                msq_enqueue(&ms_queue, &tmp_ms_data->ms_node);
        }
}
void smp_ms_queue_dyn_alloc_get(int offset)
{
        struct allocator* malloc;
        int i = offset;
        while (i < offset + percpu_ms_queue_test_number) {
                tagged_ptr_t dequeue_ptr = tp_new_none();
                while ((dequeue_ptr = msq_dequeue(&ms_queue)) == 0)
                        ;
                struct ms_test_data* get_ptr = container_of(
                        tp_get_ptr(dequeue_ptr), struct ms_test_data, ms_node);
                // the current is dequeue node
                // but the data is one the next node
                struct ms_test_data* next_ptr =
                        container_of(tp_get_ptr(get_ptr->ms_node.next),
                                     struct ms_test_data,
                                     ms_node);
                ms_data_test_seq[i] = next_ptr->data;
                malloc = per_cpu(kallocator, get_ptr->allocator_id);
                malloc->m_free(malloc, get_ptr);
                lock_cas(&cas_lock);
                cas_add_value++;
                unlock_cas(&cas_lock);
                i++;
        }
}
int smp_ms_queue_dyn_alloc_test(void)
{
        if (percpu(cpu_number) == BSP_ID) {
                smp_ms_queue_dyn_alloc_init();
                dyn_have_inited = true;
        } else {
                while (!dyn_have_inited)
                        ;
        }
#ifdef NR_CPUS
        if (NR_CPUS % 2 && NR_CPUS - 1 == percpu(cpu_number))
                return 0;
#endif
        if (percpu(cpu_number) % 2) {
                smp_ms_queue_dyn_alloc_put((percpu(cpu_number) / 2)
                                           * percpu_ms_queue_test_number);
        } else {
                smp_ms_queue_dyn_alloc_get((percpu(cpu_number) / 2)
                                           * percpu_ms_queue_test_number);
        }
        return 0;
}
bool smp_ms_queue_dyn_alloc_check(void)
{
        // if (percpu(cpu_number) == BSP_ID) {
        //         for (int i = 0; i < ms_data_len; i++) {
        //                 if (i % 10 == 0 && i)
        //                         pr_info("\n");
        //                 pr_info("%d\t", ms_data_test_seq[i]);
        //         }
        //         pr_info("\n");
        // }
        // pr_info("dyn get time %d\n",cas_add_value);
        return cas_add_value == ms_data_len;
}