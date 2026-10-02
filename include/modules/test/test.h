#ifndef _RENDEZVOS_TEST_H_
#define _RENDEZVOS_TEST_H_

#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <common/stdbool.h>
// #define DEBUG
#ifdef DEBUG
#define debug pr_debug
#else
#define debug pr_off
#endif
/**
 * @brief Max registered single-CPU / SMP test-case slots.
 */
#define MAX_SINGLE_TEST_CASE 10
#define MAX_SMP_TEST_CASE    10

/**
 * @brief BSP test thread: run single-CPU then SMP test suites, then power off.
 *
 * Built only when @c RENDEZVOS_TEST is enabled.
 */
void* BSP_test(void* arg);
/**
 * @brief AP test thread: run the SMP suite only.
 */
void* AP_test(void* arg);
/**
 * @brief Create the per-CPU test thread ( @p is_bsp_test selects BSP or AP).
 *
 * Call when @c RENDEZVOS_TEST is enabled, every cpu create once.
 */
error_t create_test_thread(bool is_bsp_test);

/**
 * @brief Run registered single-CPU cases on BSP in order and stops on first
 * failure.
 */
void single_cpu_test(void);
/**
 * @brief Run registered SMP cases.
 * There's a lot of cross-cpu barrier,
 * so the SMP will run one case, and wait other cpu finish this case, 
 * BSP will check whether the result is right if @c check_result not NULL,
 * If pass, the SMP will test the next case.
 * else, finish the test and BSP_test will power off.
 */
void multi_cpu_test(void);

/*
 * Single-CPU case entry points
 */
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

/**
 * @brief One BSP-only case: test() returns 0 on pass.
 *
 * [中文临时对照 — 审阅后可删]
 * @brief 一条仅 BSP 用例：test() 返回 0 表示通过。
 */
struct single_test_case {
        int (*test)(void);
        char name[32];
};

/**
 * @brief One SMP case: all CPUs run test(); optional BSP-only check_result.
 *
 * [中文临时对照 — 审阅后可删]
 * @brief 一条 SMP 用例：所有 CPU 跑 test()；可选仅 BSP 的 check_result。
 */
struct smp_test_case {
        int (*test)(void);
        char name[32];
        bool (*check_result)(void);
};

#endif
