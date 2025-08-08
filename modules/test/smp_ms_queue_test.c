#include <modules/test/test.h>
#include <modules/log/log.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/smp/percpu.h>
struct ms_test_data {
        ms_queue_node_t ms_node;
        int data;
};
extern int BSP_ID;
#define percpu_ms_queue_test_number 10000
#ifdef NR_CPUS
#define ms_data_len percpu_ms_queue_test_number *NR_CPUS / 2
#else
#define ms_data_len \
        percpu_ms_queue_test_number * 4 / 2 /*the default of NR_CPU is 4*/
#endif
struct ms_test_data ms_data[ms_data_len];
struct ms_test_data dummy;
ms_queue_t ms_queue;
volatile bool have_inited = false;
int ms_data_test_seq[ms_data_len] = {0};
void smp_ms_queue_init(void)
{
        dummy.data = -1;
        msq_init(&ms_queue, &dummy.ms_node);
        for (int i = 0; i < ms_data_len; i++) {
                ms_data[i].ms_node.next = tagged_ptr_none();
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
                tagged_ptr_t dequeue_ptr = tagged_ptr_none();
                while ((dequeue_ptr = msq_dequeue(&ms_queue)) == 0)
                        ;
                struct ms_test_data *get_ptr =
                        container_of(tagged_ptr_unpack_ptr(dequeue_ptr),
                                     struct ms_test_data,
                                     ms_node);
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
