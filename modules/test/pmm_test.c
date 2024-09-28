#include <modules/test/test.h>
#include <shampoos/error.h>
#include <shampoos/mm/buddy_pmm.h>
#define PPN_TEST_CASE_NUM 10

extern struct buddy buddy_pmm;
void pmm_test(void)
{
        u32 alloc_ppn[PPN_TEST_CASE_NUM];

        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3, ZONE_NORMAL);
                if (alloc_ppn[i] == -ENOMEM) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3, ZONE_NORMAL);
                if (alloc_ppn[i] == -ENOMEM) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
                if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        // try to alloc all the memory,then try to alloc will lead to an error
        // if no such an error, the boundary conditions is error
        while (buddy_pmm.zone->zone_total_avaliable_pages) {
                alloc_ppn[buddy_pmm.zone->zone_total_avaliable_pages
                          % PPN_TEST_CASE_NUM] =
                        buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        }
        if (buddy_pmm.pmm_alloc(1, ZONE_NORMAL) != -ENOMEM) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        // then we free some of the pages
        // and try to alloc again
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (buddy_pmm.pmm_free(alloc_ppn[i], 1)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
                if (alloc_ppn[i] == -ENOMEM) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              1,
                              alloc_ppn[i]);
        }
        if (buddy_pmm.pmm_alloc(1, ZONE_NORMAL) != -ENOMEM) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        pr_info("[ TEST ] PASS: pmm test ok!\n");
        return;
pmm_test_error:
        pr_error("pmm test error!\n");
        return;
}