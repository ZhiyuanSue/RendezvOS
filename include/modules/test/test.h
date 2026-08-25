#ifndef _RENDEZVOS_TEST_H_
#define _RENDEZVOS_TEST_H_

#include <modules/log/log.h>
#include <rendezvos/error.h>
// #define DEBUG
#ifdef DEBUG
#define debug pr_debug
#else
#define debug pr_off
#endif
#define MAX_SINGLE_TEST_CASE 10
#define MAX_SMP_TEST_CASE    10

void* BSP_test(void* arg);
void* AP_test(void* arg);
error_t create_test_thread(bool is_bsp_test);

void single_cpu_test(void);
void multi_cpu_test(void);

int pmm_test(void);
int arch_vmm_test(void);
int rb_tree_test(void);
int kmalloc_test(void);
int test_pci_scan(void);
int ipc_test(void);
int ipc_multi_round_test(void);
int single_port_test(void);
int single_timer_test(void);
int page_slice_test(void);

/*
 * SMP cases: either every CPU's test() return is checked, or (when
 * check_result != NULL) only BSP runs check_result() and per-CPU returns
 * are ignored — see smp_thread_affinity_* for the latter.
 */
int smp_lock_test(void);
bool smp_lock_check(void);
int smp_kmalloc_test(void);
int smp_ms_queue_test(void);
bool smp_ms_queue_check(void);
int smp_ms_queue_dyn_alloc_test(void);
bool smp_ms_queue_dyn_alloc_check(void);
int smp_ms_queue_check_test(void);
bool smp_ms_queue_check_test_check(void);
int smp_log_test(void);
bool smp_log_check(void);
int smp_ipc_test(void);
int smp_port_robustness_test(void);
int smp_thread_affinity_test(void);
bool smp_thread_affinity_check(void);

struct single_test_case {
        int (*test)(void);
        char name[32];
};

struct smp_test_case {
        int (*test)(void);
        char name[32];
        bool (*check_result)(void);
};

#endif