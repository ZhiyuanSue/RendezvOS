#ifndef _RENDEZVOS_TEST_H_
#define _RENDEZVOS_TEST_H_

#include <modules/log/log.h>
// #define DEBUG
#ifdef DEBUG
#define debug pr_debug
#else
#define debug pr_off
#endif
#define MAX_SINGLE_TEST_CASE 5
#define MAX_SMP_TEST_CASE    5

void single_cpu_test(void);
void multi_cpu_test(void);

int pmm_test(void);
int arch_vmm_test(void);
int rb_tree_test(void);
int nexus_test(void);
int spmalloc_test(void);

/*in smp case
one test function cannot easily be checked
so we designed two ways
if each smp_test can return a value,just check all the values for every cpu
else if the smp test case check function can work,
we use that check the result at cpu 0
*/
int smp_lock_test(void);
int smp_lock_check(void);
int smp_nexus_test(void);
int smp_spmalloc_test(void);
int smp_log_test(void);
int smp_log_check(void);

struct single_test_case {
        int (*test)(void);
        char name[32];
};

struct smp_test_case {
        int (*test)(void);
        char name[32];
        int (*check_result)(void);
};

#endif