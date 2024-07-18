#include <modules/test/test.h>
#include <shampoos/error.h>
#include <shampoos/mm/buddy_pmm.h>
#define PPN_TEST_CASE_NUM 10
extern struct buddy buddy_pmm;
void pmm_test(void) {
	pr_info("start pmm test\n");
	u32 alloc_ppn[PPN_TEST_CASE_NUM];

	for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
		alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3);
		if (alloc_ppn[i] == -ENOMEM) {
			pr_error("alloc error\n");
			goto pmm_test_error;
		} else
			pr_debug("try to get %x pages ,and alloc ppn 0x%x\n", i * 2 + 3,
					 alloc_ppn[i]);
	}
	for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
		if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
			pr_error("free error\n");
			goto pmm_test_error;
		} else
			pr_debug("free ppn 0x%x success\n", alloc_ppn[i]);
	}
	for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
		alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3);
		if (alloc_ppn[i] == -ENOMEM) {
			pr_error("alloc error\n");
			goto pmm_test_error;
		} else
			pr_debug("try to get %x pages ,and alloc ppn 0x%x\n", i * 2 + 3,
					 alloc_ppn[i]);
		if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
			pr_error("free error\n");
			goto pmm_test_error;
		} else
			pr_debug("free ppn 0x%x success\n", alloc_ppn[i]);
	}
	pr_info("pmm test pass!\n");
	return;
pmm_test_error:
	pr_error("pmm test error!\n");
	return;
}