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

/*
 * Test phase gate (core tests + upper-layer tests orchestration).
 *
 * This is part of the test subsystem: if `RENDEZVOS_TEST` is disabled, upper
 * layers should not wait for this gate.
 */
typedef enum core_test_phase {
        CORE_TEST_PHASE_BOOT = 0,
        CORE_TEST_PHASE_CORE_TESTS = 1,
        CORE_TEST_PHASE_UPPER_TESTS = 2,
        CORE_TEST_PHASE_DONE = 3,
} core_test_phase_t;

core_test_phase_t core_test_phase_get(void);
void core_test_phase_set(core_test_phase_t phase);

void* BSP_test(void* arg);
void* AP_test(void* arg);
error_t create_test_thread(bool is_bsp_test);

void single_cpu_test(void);
void multi_cpu_test(void);

int pmm_test(void);
int arch_vmm_test(void);
int rb_tree_test(void);
int nexus_test(void);
int kmalloc_test(void);
int test_pci_scan(void);
int ipc_test(void);
int ipc_multi_round_test(void);
int single_port_test(void);

/*in smp case
one test function cannot easily be checked
so we designed two ways
if each smp_test can return a value,just check all the values for every cpu
else if the smp test case check function can work,
we use that check the result at cpu 0
*/
int smp_lock_test(void);
bool smp_lock_check(void);
int smp_nexus_test(void);
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