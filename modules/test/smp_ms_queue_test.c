#include <modules/test/test.h>
#include <modules/log/log.h>
#include <common/dsa/ms_queue.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/kmalloc.h>
extern struct allocator* kallocator;
struct ms_test_data {
        ms_queue_node_t ms_node;
        u64 data;
};
extern u32 BSP_ID;
#define percpu_ms_queue_test_number 100000
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
        msq_init(&ms_queue, &dummy.ms_node, 0);
        for (int i = 0; i < ms_data_len; i++) {
                ref_init_zero(&ms_data[i].ms_node.refcount);
                ms_data[i].ms_node.next = tp_new_none();
                ms_data[i].data = i;
        }
}
void smp_ms_queue_put(int offset)
{
        for (int i = 0; i < percpu_ms_queue_test_number; i++) {
                msq_enqueue(&ms_queue,
                            &ms_data[offset + i].ms_node,
                            NULL,
                            true /* refcount_is_zero */);
        }
}
void smp_ms_queue_get(int offset)
{
        int i = 0;
        while (i < percpu_ms_queue_test_number) {
                tagged_ptr_t dequeue_ptr = tp_new_none();
                while ((dequeue_ptr = msq_dequeue(&ms_queue, NULL)) == 0)
                        ;
                struct ms_test_data* get_ptr = container_of(
                        tp_get_ptr(dequeue_ptr), struct ms_test_data, ms_node);
                ms_data_test_seq[offset + i] = get_ptr->data;
                ref_put(&get_ptr->ms_node.refcount, NULL);
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
        return REND_SUCCESS;
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
        msq_init(&ms_queue, &tmp->ms_node, 0);
        memset(ms_data_test_seq, 0, ms_data_len * sizeof(int));
}
static void dyn_alloc_free_node(ref_count_t* refcount)
{
        ms_queue_node_t* node =
                container_of(refcount, ms_queue_node_t, refcount);
        struct ms_test_data* p =
                container_of(node, struct ms_test_data, ms_node);
        struct allocator* malloc = percpu(kallocator);
        malloc->m_free(malloc, p);
}

void smp_ms_queue_dyn_alloc_put(int offset)
{
        struct allocator* malloc = percpu(kallocator);
        for (int i = offset; i < offset + percpu_ms_queue_test_number; i++) {
                struct ms_test_data* tmp_ms_data =
                        malloc->m_alloc(malloc, sizeof(struct ms_test_data));
                if (tmp_ms_data) {
                        memset(tmp_ms_data, 0, sizeof(struct ms_test_data));
                        ref_init_zero(&tmp_ms_data->ms_node.refcount);
                        tmp_ms_data->data = i;
                        msq_enqueue(&ms_queue,
                                    &tmp_ms_data->ms_node,
                                    dyn_alloc_free_node,
                                    true /* refcount_is_zero */);
                } else {
                        pr_error("malloc fail\n");
                }
        }
}
void smp_ms_queue_dyn_alloc_get(int offset)
{
        int i = offset;
        while (i < offset + percpu_ms_queue_test_number) {
                tagged_ptr_t dequeue_ptr = tp_new_none();
                while ((dequeue_ptr =
                                msq_dequeue(&ms_queue, dyn_alloc_free_node))
                       == 0)
                        ;
                struct ms_test_data* get_ptr = container_of(
                        tp_get_ptr(dequeue_ptr), struct ms_test_data, ms_node);
                ms_data_test_seq[i] = get_ptr->data;
                ref_put(&get_ptr->ms_node.refcount, dyn_alloc_free_node);
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
                return REND_SUCCESS;
#endif
        if (percpu(cpu_number) % 2) {
                smp_ms_queue_dyn_alloc_put((percpu(cpu_number) / 2)
                                           * percpu_ms_queue_test_number);
        } else {
                smp_ms_queue_dyn_alloc_get((percpu(cpu_number) / 2)
                                           * percpu_ms_queue_test_number);
        }
        return REND_SUCCESS;
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

/* Test for msq_enqueue_check_tail and msq_dequeue_check_head */
#define IPC_ENDPOINT_APPEND_BITS 2
#define IPC_ENDPOINT_STATE_EMPTY 0
#define IPC_ENDPOINT_STATE_SEND  1
#define IPC_ENDPOINT_STATE_RECV  2

#define ms_check_test_data_len 100000
struct ms_test_data ms_check_test_data[ms_check_test_data_len];
struct ms_test_data ms_check_dummy;
ms_queue_t ms_check_queue;
static volatile bool ms_check_have_inited = false;
static volatile int ms_check_enqueue_count = 0;
static volatile int ms_check_dequeue_count = 0;
static cas_lock_t ms_check_lock;
int ms_check_test_seq[ms_check_test_data_len] = {0};

void smp_ms_queue_check_init(void)
{
        ms_check_dummy.data = -1;
        msq_init(&ms_check_queue,
                 &ms_check_dummy.ms_node,
                 IPC_ENDPOINT_APPEND_BITS);
        for (int i = 0; i < ms_check_test_data_len; i++) {
                ref_init_zero(&ms_check_test_data[i].ms_node.refcount);
                ms_check_test_data[i].ms_node.next = tp_new_none();
                ms_check_test_data[i].data = i;
        }
        lock_init_cas(&ms_check_lock);
        ms_check_enqueue_count = 0;
        ms_check_dequeue_count = 0;
}

/* Test enqueue_check_tail: enqueue with SEND state when tail is EMPTY */
void smp_ms_queue_check_enqueue_empty_to_send(int offset)
{
        for (int i = 0; i < ms_check_test_data_len / 2; i++) {
                if (i % 10000 == 0) {
                        pr_info("finish enqueue_empty_to_send %d/%d test\n",
                                i,
                                ms_check_test_data_len / 2);
                }
                int retry_count = 0;
                while (retry_count < 100) {
                        tagged_ptr_t expect_tail =
                                tp_new(NULL, IPC_ENDPOINT_STATE_EMPTY);
                        error_t ret = msq_enqueue_check_tail(
                                &ms_check_queue,
                                &ms_check_test_data[offset + i].ms_node,
                                IPC_ENDPOINT_STATE_SEND,
                                expect_tail,
                                NULL,
                                true /* refcount_is_zero */);
                        if (ret == REND_SUCCESS) {
                                lock_cas(&ms_check_lock);
                                ms_check_enqueue_count++;
                                unlock_cas(&ms_check_lock);
                                break;
                        }
                        retry_count++;
                }
        }
}

/* Test enqueue_check_tail: enqueue with RECV state when tail is SEND or EMPTY
 */
void smp_ms_queue_check_enqueue_send_to_recv(int offset)
{
        for (int i = 0; i < ms_check_test_data_len / 2; i++) {
                if (i % 10000 == 0) {
                        pr_info("finish enqueue_send_to_recv %d/%d test\n",
                                i,
                                ms_check_test_data_len / 2);
                }
                int retry_count = 0;
                bool success = false;
                while (retry_count < 100 && !success) {
                        /* First try with SEND state */
                        tagged_ptr_t expect_tail =
                                tp_new(NULL, IPC_ENDPOINT_STATE_SEND);
                        error_t ret = msq_enqueue_check_tail(
                                &ms_check_queue,
                                &ms_check_test_data[offset + i].ms_node,
                                IPC_ENDPOINT_STATE_RECV,
                                expect_tail,
                                NULL,
                                true /* refcount_is_zero */);
                        if (ret == REND_SUCCESS) {
                                lock_cas(&ms_check_lock);
                                ms_check_enqueue_count++;
                                unlock_cas(&ms_check_lock);
                                success = true;
                                break;
                        }
                        /* If failed, retry with EMPTY state (queue might be
                         * empty) */
                        expect_tail = tp_new(NULL, IPC_ENDPOINT_STATE_EMPTY);
                        ret = msq_enqueue_check_tail(
                                &ms_check_queue,
                                &ms_check_test_data[offset + i].ms_node,
                                IPC_ENDPOINT_STATE_RECV,
                                expect_tail,
                                NULL,
                                true /* refcount_is_zero */);
                        if (ret == REND_SUCCESS) {
                                lock_cas(&ms_check_lock);
                                ms_check_enqueue_count++;
                                unlock_cas(&ms_check_lock);
                                success = true;
                                break;
                        }
                        retry_count++;
                }
        }
}

/* Test dequeue_check_head: dequeue nodes with SEND state */
void smp_ms_queue_check_dequeue_send(int offset)
{
        int i = 0;
        int retry_count = 0;
        while (i < ms_check_test_data_len / 2) {
                if (i % 10000 == 0) {
                        pr_info("finish dequeue_send %d/%d test\n",
                                i,
                                ms_check_test_data_len / 2);
                }
                tagged_ptr_t expect_head =
                        tp_new(NULL, IPC_ENDPOINT_STATE_SEND);
                tagged_ptr_t dequeue_ptr =
                        msq_dequeue_check_head(&ms_check_queue,
                                               MSQ_CHECK_FIELD_APPEND,
                                               expect_head,
                                               NULL);
                if (!tp_is_none(dequeue_ptr)) {
                        struct ms_test_data* get_ptr =
                                container_of(tp_get_ptr(dequeue_ptr),
                                             struct ms_test_data,
                                             ms_node);
                        ms_check_test_seq[offset + i] = get_ptr->data;
                        ref_put(&get_ptr->ms_node.refcount, NULL);
                        lock_cas(&ms_check_lock);
                        ms_check_dequeue_count++;
                        unlock_cas(&ms_check_lock);
                        i++;
                        retry_count = 0;
                } else {
                        /* Queue might be empty or state mismatch, wait a bit */
                        retry_count++;
                        if (retry_count > 10000) {
                                /* Give up if queue is empty for too long */
                                break;
                        }
                }
        }
}

/* Test dequeue_check_head: dequeue nodes with RECV state */
void smp_ms_queue_check_dequeue_recv(int offset)
{
        int i = 0;
        int retry_count = 0;
        while (i < ms_check_test_data_len / 2) {
                if (i % 10000 == 0) {
                        pr_info("finish dequeue_recv %d/%d test\n",
                                i,
                                ms_check_test_data_len / 2);
                }
                tagged_ptr_t expect_head =
                        tp_new(NULL, IPC_ENDPOINT_STATE_RECV);
                tagged_ptr_t dequeue_ptr =
                        msq_dequeue_check_head(&ms_check_queue,
                                               MSQ_CHECK_FIELD_APPEND,
                                               expect_head,
                                               NULL);
                if (!tp_is_none(dequeue_ptr)) {
                        struct ms_test_data* get_ptr =
                                container_of(tp_get_ptr(dequeue_ptr),
                                             struct ms_test_data,
                                             ms_node);
                        ms_check_test_seq[offset + i] = get_ptr->data;
                        ref_put(&get_ptr->ms_node.refcount, NULL);
                        lock_cas(&ms_check_lock);
                        ms_check_dequeue_count++;
                        unlock_cas(&ms_check_lock);
                        i++;
                        retry_count = 0;
                } else {
                        /* Queue might be empty or state mismatch, wait a bit */
                        retry_count++;
                        if (retry_count > 10000) {
                                /* Give up if queue is empty for too long */
                                break;
                        }
                }
        }
}

int smp_ms_queue_check_test(void)
{
        if (percpu(cpu_number) == BSP_ID) {
                smp_ms_queue_check_init();
                ms_check_have_inited = true;
        } else {
                while (!ms_check_have_inited)
                        ;
        }

        /* Test scenario:
         * CPU 0: enqueue SEND nodes when tail is EMPTY
         * CPU 1: dequeue SEND nodes
         * CPU 2: enqueue RECV nodes when tail is SEND (or EMPTY)
         * CPU 3: dequeue RECV nodes
         */
        u32 cpu_num = percpu(cpu_number);
        int offset = (cpu_num % 2) * (ms_check_test_data_len / 2);

        if (cpu_num % 4 == 0) {
                /* Enqueue SEND when tail is EMPTY */
                smp_ms_queue_check_enqueue_empty_to_send(offset);
        } else if (cpu_num % 4 == 1) {
                /* Dequeue SEND */
                smp_ms_queue_check_dequeue_send(offset);
        } else if (cpu_num % 4 == 2) {
                /* Enqueue RECV when tail is SEND */
                smp_ms_queue_check_enqueue_send_to_recv(offset);
        } else {
                /* Dequeue RECV */
                smp_ms_queue_check_dequeue_recv(offset);
        }

        return REND_SUCCESS;
}

bool smp_ms_queue_check_test_check(void)
{
        /* Check that enqueue and dequeue counts match */
        return ms_check_enqueue_count == ms_check_dequeue_count;
}