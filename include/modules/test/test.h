#ifndef _SHAMPOOS_TEST_H_
#define _SHAMPOOS_TEST_H_

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

struct test_case {
        int (*test)(void);
        char name[32];
};

#endif