#ifndef _SHAMPOOS_TEST_H_
#define _SHAMPOOS_TEST_H_

#include <modules/log/log.h>
// #define DEBUG
#ifdef DEBUG
#define debug pr_debug
#else
#define debug pr_off
#endif

void pmm_test(void);
void arch_vmm_test(void);
void test(void);
void rb_tree_test(void);

#endif